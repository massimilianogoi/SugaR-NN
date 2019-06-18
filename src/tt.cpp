/*
  SugaR, a UCI chess playing engine derived from Stockfish
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2019 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  SugaR is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  SugaR is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstring>   // For std::memset
#include <iostream>
#include <thread>
#include <fstream> //from kellykynyama mcts
#include "uci.h"
using std::string;
#include <sstream>
#include <string>
#include <sstream>
#include <vector>
#include <iterator>
#include "position.h"
#include "thread.h"

#include "bitboard.h"
#include "misc.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"

//from Kelly Begin
using namespace std;

MCTSHashTable mctsHT;
//from Kelly end

//https://stackoverflow.com/questions/236129/most-elegant-way-to-split-a-string
template<typename Out>
void split(const std::string &s, char delim, Out result) {
	std::stringstream ss;
	ss.str(s);
	std::string item;
	while (std::getline(ss, item, delim)) {
		*(result++) = item;
	}
}

std::vector<std::string> split(const std::string &s, char delim) {
	std::vector<std::string> elems;
	split(s, delim, std::back_inserter(elems));
	return elems;
}

TranspositionTable TT; // Our global transposition table

/// TTEntry::save populates the TTEntry with a new node's data, possibly
/// overwriting an old position. Update is not atomic and can be racy.

void TTEntry::save(Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev) {

  assert(d / ONE_PLY * ONE_PLY == d);

  // Preserve any existing move for the same position
  if (m || (k >> 48) != key16)
      move16 = (uint16_t)m;

  // Overwrite less valuable entries
  if (  (k >> 48) != key16
      ||(d - DEPTH_OFFSET) / ONE_PLY > depth8 - 4
      || b == BOUND_EXACT)
  {
      assert((d - DEPTH_OFFSET) / ONE_PLY >= 0);

      key16     = (uint16_t)(k >> 48);
      value16   = (int16_t)v;
      eval16    = (int16_t)ev;
      genBound8 = (uint8_t)(TT.generation8 | uint8_t(pv) << 2 | b);
      depth8    = (uint8_t)((d - DEPTH_OFFSET) / ONE_PLY);
  }
}


/// TranspositionTable::resize() sets the size of the transposition table,
/// measured in megabytes. Transposition table consists of a power of 2 number
/// of clusters and each cluster consists of ClusterSize number of TTEntry.

void TranspositionTable::resize(size_t mbSize) {

  Threads.main()->wait_for_search_finished();

  clusterCount = mbSize * 1024 * 1024 / sizeof(Cluster);

  free(mem);
  mem = malloc(clusterCount * sizeof(Cluster) + CacheLineSize - 1);

  if (!mem)
  {
      std::cerr << "Failed to allocate " << mbSize
                << "MB for transposition table." << std::endl;
      exit(EXIT_FAILURE);
  }

  table = (Cluster*)((uintptr_t(mem) + CacheLineSize - 1) & ~(CacheLineSize - 1));
  clear();
}


/// TranspositionTable::clear() initializes the entire transposition table to zero,
//  in a multi-threaded way.

void TranspositionTable::clear() {

  std::vector<std::thread> threads;

  for (size_t idx = 0; idx < Options["Threads"]; ++idx)
  {
      threads.emplace_back([this, idx]() {

          // Thread binding gives faster search on systems with a first-touch policy
          if (Options["Threads"] > 8)
              WinProcGroup::bindThisThread(idx);

          // Each thread will zero its part of the hash table
          const size_t stride = clusterCount / Options["Threads"],
                       start  = stride * idx,
                       len    = idx != Options["Threads"] - 1 ?
                                stride : clusterCount - start;

          std::memset(&table[start], 0, len * sizeof(Cluster));
      });
  }

  for (std::thread& th: threads)
      th.join();
}

void TranspositionTable::set_hash_file_name(const std::string& fname) { hashfilename = fname; }

bool TranspositionTable::save() {
	std::ofstream b_stream(hashfilename,
		std::fstream::out | std::fstream::binary);
	if (b_stream)
	{
		//b_stream.write(reinterpret_cast<char const *>(table), clusterCount * sizeof(Cluster));
		for (unsigned long long i = 0; i < clusterCount * sizeof(Cluster); i += (1 << 30)) { //1GB
#ifndef __min
        #define __min(a,b) (((a) < (b)) ? (a) : (b))
#endif
		    unsigned long long j = __min((1 << 30), (clusterCount * sizeof(Cluster)) - i);
			b_stream.write(reinterpret_cast<char const *>(table) + i, j);
		}
		return (b_stream.good());
	}
	return false;
}

void TranspositionTable::load() {
	//file size: https://stackoverflow.com/questions/2409504/using-c-filestreams-fstream-how-can-you-determine-the-size-of-a-file
	std::ifstream file;
	file.open(hashfilename, std::ios::in | std::ios::binary);
	file.ignore(std::numeric_limits<std::streamsize>::max());
	std::streamsize size = file.gcount();
	file.clear();   //  Since ignore will have set eof.
	resize(size_t(size / 1024 / 1024));
	file.seekg(0, std::ios::beg);
	file.read(reinterpret_cast<char *>(table), clusterCount * sizeof(Cluster));
}

enum { SAN_MOVE_NORMAL, SAN_PAWN_CAPTURE };

//taken from stockfish-TCEC6-PA_GTB
template <int MoveType> inline Move test_move(Position &pos, Square fromsquare, Square tosquare, PieceType promotion)
{
	Move move;

	if (MoveType == SAN_MOVE_NORMAL) {
		if (promotion != NO_PIECE_TYPE) {
			move = make<PROMOTION>(fromsquare, tosquare, promotion);
		}
		else {
			move = make<NORMAL>(fromsquare, tosquare);
		}
	}
	else if (MoveType == SAN_PAWN_CAPTURE) {
		if (pos.ep_square() == tosquare) {
			move = make<ENPASSANT>(fromsquare, tosquare);
		}
		else {
			if (promotion != NO_PIECE_TYPE) {
				move = make<PROMOTION>(fromsquare, tosquare, promotion);
			}
			else {
				move = make<NORMAL>(fromsquare, tosquare);
			}
		}
	}
	//if (pos.pseudo_legal(move) && pos.legal(move, pos.pinned_pieces(pos.side_to_move()))) {
	if (pos.pseudo_legal(move) && pos.legal(move)) {
#ifdef SAN_DEBUG
		sync_cout << "found a move: " << move_to_uci(move, false) << sync_endl;
#endif
		return move;
	}
	else {
#ifdef SAN_DEBUG
		sync_cout << "invalid move: " << move_to_uci(move, false) << sync_endl;
#endif
		return MOVE_NONE; // invalid;
	}
	return MOVE_NONE;
}

//taken from stockfish-TCEC6-PA_GTB
Move san_to_move(Position& pos, std::string& str)
{
	std::string uci = str;
	PieceType promotion = NO_PIECE_TYPE;
	bool castles = false;
	bool capture = false;
	Move move = MOVE_NONE;

	size_t idx = uci.find_first_of("+#");
	if (idx != std::string::npos) {
		uci.erase(idx); // erase to end of the string
	}
	idx = uci.find_first_of("=");
	if (idx != std::string::npos) {
		char promo = uci.at(idx);
		switch (promo) {
		case 'Q': promotion = QUEEN; break;
		case 'R': promotion = ROOK; break;
		case 'B': promotion = BISHOP; break;
		case 'N': promotion = KNIGHT; break;
		default: return MOVE_NONE; // invalid
		}
		uci.erase(idx);
	}
	else { // check the last char, is it QRBN?
		char promo2 = uci.at(uci.size() - 1);
		switch (promo2) {
		case 'Q': promotion = QUEEN; break;
		case 'R': promotion = ROOK; break;
		case 'B': promotion = BISHOP; break;
		case 'N': promotion = KNIGHT; break;
		default:; // nixda
		}
		if (promotion != NO_PIECE_TYPE)
			uci.erase(uci.size() - 1);
	}
	idx = uci.find_first_of("x");
	if (idx != std::string::npos) {
		capture = true;
		uci.erase(idx, 1);
	}

	char piece = str.at(0);
	PieceType piecetype;
	std::string thepiece;

	switch (piece) {
	case 'N': piecetype = KNIGHT; break;
	case 'B': piecetype = BISHOP; break;
	case 'R': piecetype = ROOK; break;
	case 'Q': piecetype = QUEEN; break;
	case 'K': piecetype = KING; break;
	case '0':
	case 'O':
		castles = true; piecetype = NO_PIECE_TYPE; break;
	default: piecetype = PAWN;
	}

	if (castles) { // chess 960?
		if (uci == "0-0" || uci == "O-O") {
			if (pos.side_to_move() == WHITE) {
				move = make<CASTLING>(SQ_E1, SQ_H1);
			}
			else {
				move = make<CASTLING>(SQ_E8, SQ_H8);
			}
		}
		else if (uci == "0-0-0" || uci == "O-O-O") {
			if (pos.side_to_move() == WHITE) {
				move = make<CASTLING>(SQ_E1, SQ_A1);
			}
			else {
				move = make<CASTLING>(SQ_E8, SQ_A8);
			}
		}
		if (pos.pseudo_legal(move) && pos.legal(move)) {
			return move;
		}
		return MOVE_NONE; // invalid
	}

	// normal move or promotion
	int torank = uci.at(uci.size() - 1) - '1';
	int tofile = uci.at(uci.size() - 2) - 'a';
	int disambig_r = -1;
	int disambig_f = -1;
	if (piecetype != PAWN && piecetype != KING && uci.size() > 3) {
		char ambig = uci.at(uci.size() - 3);
		if (ambig >= 'a' && ambig <= 'h') {
			disambig_f = ambig - 'a';
		}
		else if (ambig >= '1' && ambig <= '8') {
			disambig_r = ambig - '1';
		}
		else {
			return MOVE_NONE; // invalid;
		}
	}

	Square tosquare = Square((torank * 8) + tofile);
	const Square *pl;
	int piececount;

	switch (piecetype) {
	case PAWN:
		pl = pos.squares<PAWN>(pos.side_to_move());
		piececount = pos.count<PAWN>(pos.side_to_move());
		break;
	case KNIGHT:
		pl = pos.squares<KNIGHT>(pos.side_to_move());
		piececount = pos.count<KNIGHT>(pos.side_to_move());
		break;
	case BISHOP:
		pl = pos.squares<BISHOP>(pos.side_to_move());
		piececount = pos.count<BISHOP>(pos.side_to_move());
		break;
	case ROOK:
		pl = pos.squares<ROOK>(pos.side_to_move());
		piececount = pos.count<ROOK>(pos.side_to_move());
		break;
	case QUEEN:
		pl = pos.squares<QUEEN>(pos.side_to_move());
		piececount = pos.count<QUEEN>(pos.side_to_move());
		break;
	case KING:
		pl = pos.squares<KING>(pos.side_to_move());
		piececount = pos.count<KING>(pos.side_to_move());
		break;
	default:
		return MOVE_NONE; // invalid
	}

	if (piececount == 1) {
		if (piecetype != PAWN || !capture) {
			move = test_move<SAN_MOVE_NORMAL>(pos, *pl, tosquare, promotion);
		}
		else {
			move = test_move<SAN_PAWN_CAPTURE>(pos, *pl, tosquare, promotion);
		}
		if (move != MOVE_NONE) {
			return move;
		}
		else {
			return MOVE_NONE;
		}
	}
	else if (piececount > 1) {
		Square s;
		while ((s = *pl++) != SQ_NONE) {
			Square ss = SQ_NONE;
			if (disambig_r >= 0 || disambig_f >= 0) {
				if (disambig_r >= 0 && rank_of(s) == Rank(disambig_r)) {
					ss = s;
				}
				else if (disambig_f >= 0 && file_of(s) == File(disambig_f)) {
					ss = s;
				}
			}
			else {
				ss = s;
			}
			if (ss != SQ_NONE) {
				if (piecetype != PAWN || !capture) {
					move = test_move<SAN_MOVE_NORMAL>(pos, ss, tosquare, promotion);
				}
				else {
					move = test_move<SAN_PAWN_CAPTURE>(pos, ss, tosquare, promotion);
				}
				if (move != MOVE_NONE) {
					return move;
				}
				else {
					; // don't return, we just need to keep trying
				}
			}
		}
	}
	return MOVE_NONE;
}

//taken from stockfish-TCEC6-PA_GTB
Value uci_to_score(std::string &str)
{
	Value uci = (Value)atoi(str.c_str());
	Value v = VALUE_NONE;

	if (uci > 32000) {
		v = VALUE_MATE - (32767 - uci);
	}
	else if (uci < -32000) {
		v = -VALUE_MATE + (32767 + uci);
	}
	else {
		v = uci * int(PawnValueMg) / 100;
	}
	return v;
}

void TranspositionTable::load_epd_to_hash() {
	std::string line;
	std::ifstream myfile(hashfilename);
	Position pos;
	Move bm;
	int ce;
	int depth;
	generation8 = 4; //for storing the positions

	if (myfile.is_open())
	{
		while (getline(myfile, line))
		{
			std::vector<std::string> x = split(line, ';');

			//extract and set position
			std::size_t i = x[0].find("acd"); //depth searched. Is after the fen string
			StateListPtr states(new std::deque<StateInfo>(1));
			sync_cout << x[0].substr(0, i) << sync_endl;
			pos.set(x[0].substr(0, i), Options["UCI_Chess960"], &states->back(), Threads.main());
			
			//depth
			depth = std::stoi(x[0].substr(i + 4));
			sync_cout << depth << sync_endl;

			bm = MOVE_NONE;
			ce = -1000000;

			for (std::vector<int>::size_type j = 1; j <= x.size(); j++) {
				if (bm == MOVE_NONE) {
					i = x[j].find("bm ");
					if (i == 1) {
						sync_cout << x[j].substr(i + 3) << sync_endl;
						std::string stri = x[j].substr(i + 3);
						bm = san_to_move(pos, stri);
						if (bm != MOVE_NONE)
							sync_cout << "move ok" << sync_endl;
						continue;
					}
				}
				if (ce == -1000000) {
					i = x[j].find("ce ");
					if (i == 1) {
						std::string stri = x[j].substr(i + 3);
						ce = uci_to_score(stri);
						sync_cout << "ce " << ce << sync_endl;
						continue;
					}
				}
			}

			TTEntry* tte;
			bool ttHit;
			tte = TT.probe(pos.key(), ttHit);

			tte->save(pos.key(), (Value)ce, true, BOUND_EXACT, (Depth)depth, 
				bm, VALUE_NONE);
		}
		myfile.close();
	}
}

/// TranspositionTable::probe() looks up the current position in the transposition
/// table. It returns true and a pointer to the TTEntry if the position is found.
/// Otherwise, it returns false and a pointer to an empty or least valuable TTEntry
/// to be replaced later. The replace value of an entry is calculated as its depth
/// minus 8 times its relative age. TTEntry t1 is considered more valuable than
/// TTEntry t2 if its replace value is greater than that of t2.

TTEntry* TranspositionTable::probe(const Key key, bool& found) const {

  TTEntry* const tte = first_entry(key);
  const uint16_t key16 = key >> 48;  // Use the high 16 bits as key inside the cluster

  for (int i = 0; i < ClusterSize; ++i)
      if (!tte[i].key16 || tte[i].key16 == key16)
      {
          tte[i].genBound8 = uint8_t(generation8 | (tte[i].genBound8 & 0x7)); // Refresh

          return found = (bool)tte[i].key16, &tte[i];
      }

  // Find an entry to be replaced according to the replacement strategy
  TTEntry* replace = tte;
  for (int i = 1; i < ClusterSize; ++i)
      // Due to our packed storage format for generation and its cyclic
      // nature we add 263 (256 is the modulus plus 7 to keep the unrelated
      // lowest three bits from affecting the result) to calculate the entry
      // age correctly even after generation8 overflows into the next cycle.
      if (  replace->depth8 - ((263 + generation8 - replace->genBound8) & 0xF8)
          >   tte[i].depth8 - ((263 + generation8 -   tte[i].genBound8) & 0xF8))
          replace = &tte[i];

  return found = false, replace;
}


/// TranspositionTable::hashfull() returns an approximation of the hashtable
/// occupation during a search. The hash is x permill full, as per UCI protocol.

int TranspositionTable::hashfull() const {

  int cnt = 0;
  for (int i = 0; i < 1000 / ClusterSize; ++i)
      for (int j = 0; j < ClusterSize; ++j)
          cnt += (table[i].entry[j].genBound8 & 0xF8) == generation8;

  return cnt * 1000 / (ClusterSize * (1000 / ClusterSize));
}
//from Kelly begin
void expResize(std::string fileName) {

    ifstream myFile(fileName+".bin", ios::in | ios::binary);
    int load = 1;
    while (load)
    {
	    ExpEntry tempExpEntry;
	    tempExpEntry.depth = Depth(0);
	    tempExpEntry.hashKey = 0;
	    tempExpEntry.move = Move(0);
	    tempExpEntry.score = Value(0);

	    myFile.read((char*)&tempExpEntry, sizeof(tempExpEntry));

	    if (tempExpEntry.hashKey)
	    {

		    mctsInsert(tempExpEntry);
	    }
	    else
		    load = 0;



	    if (!tempExpEntry.hashKey)
		    load = 0;
    }
    myFile.close();
}

void expOpeningsLoad(char* fen)
{

	ifstream myFile(fen, ios::in | ios::binary);




	int load = 1;

	while (load)
	{
		ExpEntry tempExpEntry;
		tempExpEntry.depth = Depth(0);
		tempExpEntry.hashKey = 0;
		tempExpEntry.move = Move(0);
		tempExpEntry.score = Value(0);
		myFile.read((char*)&tempExpEntry, sizeof(tempExpEntry));

		if (tempExpEntry.hashKey)
		{
			mctsInsert(tempExpEntry);
		}
		load = 0;


		if (!tempExpEntry.hashKey)
			load = 0;
	}
	myFile.close();
}

void mctsInsert(ExpEntry tempExpEntry)
{
	// If the node already exists in the hash table, we want to return it.
	// We search in the range of all the hash table entries with key "key1".
	auto range = mctsHT.equal_range(tempExpEntry.hashKey);
	auto it1 = range.first;
	auto it2 = range.second;

	bool newNode = true;
	while (it1 != it2)
	{
		Node node = &(it1->second);

		if (node->hashKey == tempExpEntry.hashKey)
		{
			bool newChild = true;
			node->lateChild.move = tempExpEntry.move;
			node->lateChild.score = tempExpEntry.score;
			node->lateChild.depth = tempExpEntry.depth;
			newNode = false;
			for (int x = 0; x < node->sons; x++)
			{
				if (node->child[x].move == tempExpEntry.move)
				{
					newChild = false;
					node->child[x].move = tempExpEntry.move;
					node->child[x].depth = tempExpEntry.depth;
					node->child[x].score = tempExpEntry.score;
					node->child[x].visits++;
					//	node->sons++;
					node->totalVisits++;
					break;
				}
			}
			if (newChild && node->sons < MAX_CHILDREN)
			{
				node->child[node->sons].move = tempExpEntry.move;
				node->child[node->sons].depth = tempExpEntry.depth;
				node->child[node->sons].score = tempExpEntry.score;
				node->child[node->sons].visits++;
				node->sons++;
				node->totalVisits++;
			}



		}

		it1++;
	}

	if (newNode)
	{
		// Node was not found, so we have to create a new one
		NodeInfo infos;

		infos.hashKey = 0;        // Zobrist hash of pawns
		infos.sons = 0;

		infos.totalVisits = 0;// number of visits by the Monte-Carlo algorithm
		infos.child[0].move = MOVE_NONE;
		infos.child[0].depth = DEPTH_NONE;
		infos.child[0].score = VALUE_NONE;
		infos.child[0].visits = 0;
		std::memset(infos.child, 0, sizeof(Child) * 20);
		infos.lateChild.move = MOVE_NONE;;
		infos.lateChild.score = VALUE_NONE;
		infos.lateChild.depth = DEPTH_NONE;

		infos.lateChild.move = tempExpEntry.move;;
		infos.lateChild.score = tempExpEntry.score;
		infos.lateChild.depth = tempExpEntry.depth;

		infos.hashKey = tempExpEntry.hashKey;        // Zobrist hash of pawns
		infos.sons = 1;       // number of visits by the Monte-Carlo algorithm
		infos.totalVisits = 1;
		infos.child[0].move = tempExpEntry.move;
		infos.child[0].depth = tempExpEntry.depth;
		infos.child[0].score = tempExpEntry.score;
		infos.child[0].visits = 1;       // number of sons expanded by the Monte-Carlo algorithm
										 //infos.lastMove = MOVE_NONE; // the move between the parent and this node

										 //debug << "inserting into the hash table: key = " << key1 << endl;

		mctsHT.insert(make_pair(tempExpEntry.hashKey, infos));
	}
}

/// get_node() probes the Monte-Carlo hash table to find the node with the given
/// position, creating a new entry if it doesn't exist yet in the table.
/// The returned node is always valid.
Node get_node(Key key) {


	// If the node already exists in the hash table, we want to return it.
	// We search in the range of all the hash table entries with key "key1".

	Node mynode = nullptr;
	auto range = mctsHT.equal_range(key);
	auto it1 = range.first;
	auto it2 = range.second;

	if (
		it1 != mctsHT.end()
		&& 
		it2 != mctsHT.end()
		)
	{

		mynode = &(it1->second);

		while (
			it1 != it2
			&&
			it1 != mctsHT.end()
			)
		{
			Node node = &(it1->second);
			if (node->hashKey == key)
				return node;

			it1++;
		}

	}
	return mynode;
}

void loadLearningFiles(std::string fileName)
{
    std::ofstream master (fileName+".bin",
			std::ofstream::app | std::ofstream::binary);
    bool merging=true;
    int i=0;
    while (merging)
    {
      std::string index = std::to_string(i);
      std::string slaveName = fileName + index + ".bin";
      ifstream slave (slaveName, ios::in | ios::binary);
      if(!slave.good())
      {
	  merging=false;
	  i++;
      }
      else
      {
	while(slave.good())
	{
	    ExpEntry tempExpEntry;
	    tempExpEntry.depth = Depth(0);
	    tempExpEntry.hashKey = 0;
	    tempExpEntry.move = Move(0);
	    tempExpEntry.score = Value(0);

	    slave.read((char*)&tempExpEntry, sizeof(tempExpEntry));
	    if (tempExpEntry.hashKey)
	    {
	      Node node = get_node(tempExpEntry.hashKey);
	      if ((node==nullptr) || tempExpEntry.hashKey != node->hashKey)
	      {
		  master.write((char*)&tempExpEntry, sizeof(tempExpEntry));
		  mctsInsert(tempExpEntry);
	      }
	      if(node!=nullptr)
	      {
		if (tempExpEntry.hashKey == node->hashKey
			&& tempExpEntry.move == node->lateChild.move
			&& tempExpEntry.depth > node->lateChild.depth
			)
		{
		  mctsInsert(tempExpEntry);
		}
		if (tempExpEntry.hashKey == node->hashKey
			&& tempExpEntry.move != node->lateChild.move
			&& tempExpEntry.score > node->lateChild.score
			)
		{
		  master.write((char*)&tempExpEntry, sizeof(tempExpEntry));
		  mctsInsert(tempExpEntry);
		}
	      }
	    }
	    else
	    {
		slave.close();
		char slaveStr[slaveName.size() + 1];
		strcpy(slaveStr, slaveName.c_str());
		remove(slaveStr);
		i++;
	    }
	}
      }

    }
    master.close();
}
//from Kelly End
