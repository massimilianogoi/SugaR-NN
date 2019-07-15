### Overview

SugaR is a free UCI chess engine derived from Stockfish. It is
not a complete chess program and requires some UCI-compatible GUI
(e.g. XBoard with PolyGlot, eboard, Arena, Sigma Chess, Shredder, Chess
Partner, Aquarium or Fritz) in order to be used comfortably. Read the
documentation for your GUI of choice for information about how to use
SugaR with it.

This version of SugaR supports up to 128 cores. The engine defaults
to one search thread, so it is therefore recommended to inspect the value of
the *Threads* UCI parameter, and to make sure it equals the number of CPU
cores on your computer.

This version of SugaR has support for Syzygybases.


### Files

This distribution of SugaR consists of the following files:

  * Readme.md, the file you are currently reading.

  * Copying.txt, a text file containing the GNU General Public License.

  * source, a subdirectory containing the full source code, including a Makefile
    that can be used to compile SugaR on Unix-like systems.

## Uci options

Lower the Skill Level in order to make Stockfish play weaker (see also UCI_LimitStrength). Internally, MultiPV is enabled, and with a certain probability depending on the Skill Level a weaker move will be played.
										

Enable weaker play aiming for an Elo rating as set by UCI_Elo. This option overrides Skill Level.

If enabled by UCI_LimitStrength, aim for an engine strength of the given Elo. This Elo rating has been calibrated at a time control of 60s+0.6s and anchored to CCRL 40/4.
## Less Pruning Mode
Default: 0, Min: 0, Max:9

0, no MultiPV.
1, no MultiPV and corchess mode (for game play at very long time control or analysis purpose)
2-9 MultiPV and corchess mode : higher depths and longer time to reach them. So, fewer tactical shots missed, but loss of some ELO, increasingly until 9,
corresponding to multiPV = 256.
N.B. Corchess mode [https://github.com/IIvec/Stockfish/tree/corchess/]

Recommended values: from 2 to 5 ( > 5 too wide search width)

## SugaR-NN can use two parallel books
original code by Thomas Zipproth:
https://zipproth.de/Brainfish/brainfish/

## Dynamic Strategy 
_Boolean, Default: False_

To be used as additional support in the analysis of particularly complex positions.
With the increase of the score, that is how much the motor is in advantage or fundamentally closer to the checkmate.
Or In all favorable pressing situations; The advanced Pawns are penalized and the King gains more importance because
we must pay attention to the compactness and the other way around.

### NN section (Experimental Neural Networks inspired technics)
Experimental, MonteCarloTreeSearch, if activated, the engine's behaviour is similar to AlphaZero concepts.
Idea are implemented, integrated on SugaR:
	
### NN Persisted Self-Learning
_Boolean, Default: True_


- [https://github.com/Kellykinyama12/Stockfish] (montecarlo by Kelly Kinyama) only when true. This creates three files for machine learning purposes:
SugaR-NN implements a persistent learning algorithm by Kelly kyniama and Andrea Manzo.
Reads and creates the following file types:

- pawngame.bin with the learning when there are max a total of 2 pieces for white and black
- experience.bin with the learning for
  - opening variation of max 16 moves (8 half-moves) and a total of at least 7 pieces (no pawns) for white and black
  - positions with max 6 pieces (no pawns) for white and black
- One or many .bin files, each one associated to a single position biunivocally associated to the (technically, hashKey), in an opening variation of max 8 moves (16 half-moves) and a total of at least 7 pieces (no pawns) for white and black. This position is also in the experience.bin. So, these files are to speed the load in memory.

Every .bin file is so a collection of one or more positions stored with the following format (similar to in memory Stockfish Transposition Table):

- _best move_
- _board signature (hash key)_
- _best move depth_
- _best move score_

At the engine loading, there is an automatic merge to pawn.bin and experience.bin files, if we put the other ones, based on the following convention:

&lt;fileType&gt;&lt;qualityIndex&gt;.bin

where

- _fileType=&quot;experience&quot;/&quot;bin&quot;_
- _qualityIndex_ , an integer, incrementally from 0 on based on the file&#39;s quality assigned by the user (0 best quality and so on)

The opening files can be simply copied and, in case of conflict/same name, the user must choice the one to use.

N.B.

Because of disk access, to be effective, the learning must be made at no bullet time controls (less than 5 minutes/game).
#### NN Perceptron Algorithm
_Boolean, Default: False_

- [https://github.com/official-stockfish/Stockfish/compare/master...Stefano80:perceptron_new]
( Perceptron Sigmoid activation by Stefano Cardanobile) for Late Move Reductions search as training signal

#### NN MCTS Score
_Boolean, Default: False_

- [https://github.com/mcostalba/Stockfish/compare/master...joergoster:mcts_scores]
( (Montecarlo Tree Search Scores) by Jörg Oster) in main search function to an upper node.

### Syzygybases

**Configuration**

Syzygybases are configured using the UCI options "SyzygyPath",
"SyzygyProbeDepth", "Syzygy50MoveRule" and "SyzygyProbeLimit".

The option "SyzygyPath" should be set to the directory or directories that
contain the .rtbw and .rtbz files. Multiple directories should be
separated by ";" on Windows and by ":" on Unix-based operating systems.
**Do not use spaces around the ";" or ":".**

Example: `C:\tablebases\wdl345;C:\tablebases\wdl6;D:\tablebases\dtz345;D:\tablebases\dtz6`

It is recommended to store .rtbw files on an SSD. There is no loss in
storing the .rtbz files on a regular HD.

Increasing the "SyzygyProbeDepth" option lets the engine probe less
aggressively. Set this option to a higher value if you experience too much
slowdown (in terms of nps) due to TB probing.

Set the "Syzygy50MoveRule" option to false if you want tablebase positions
that are drawn by the 50-move rule to count as win or loss. This may be useful
for correspondence games (because of tablebase adjudication).

The "SyzygyProbeLimit" option should normally be left at its default value.

**What to expect**
If the engine is searching a position that is not in the tablebases (e.g.
a position with 8 pieces), it will access the tablebases during the search.
If the engine reports a very large score (typically 123.xx), this means
that it has found a winning line into a tablebase position.

If the engine is given a position to search that is in the tablebases, it
will use the tablebases at the beginning of the search to preselect all
good moves, i.e. all moves that preserve the win or preserve the draw while
taking into account the 50-move rule.
It will then perform a search only on those moves. **The engine will not move
immediately**, unless there is only a single good move. **The engine likely
will not report a mate score even if the position is known to be won.**

It is therefore clear that behaviour is not identical to what one might
be used to with Nalimov tablebases. There are technical reasons for this
difference, the main technical reason being that Nalimov tablebases use the
DTM metric (distance-to-mate), while Syzygybases use a variation of the
DTZ metric (distance-to-zero, zero meaning any move that resets the 50-move
counter). This special metric is one of the reasons that Syzygybases are
more compact than Nalimov tablebases, while still storing all information
needed for optimal play and in addition being able to take into account
the 50-move rule.


### Compiling it yourself

On Unix-like systems, it should be possible to compile SugaR
directly from the source code with the included Makefile.

SugaR has support for 32 or 64-bit CPUs, the hardware POPCNT
instruction, big-endian machines such as Power PC, and other platforms.

On Windows-like systems, it should be possible to compile SugaR
directly from the source code with the included Sugar.sln with Visual Studio 15.3 Community 
from GUI or with command scenario using Visual Studio 15.3 Community Commands Shell.

In general it is recommended to run `make help` to see a list of make
targets with corresponding descriptions. When not using the Makefile to
compile you need to manually
set/unset some switches in the compiler command line or use MSVC solution and project files provided; see file *types.h*
for a quick reference.


### Terms of use

SugaR is free, and distributed under the **GNU General Public License**
(GPL). Essentially, this means that you are free to do almost exactly
what you want with the program, including distributing it among your
friends, making it available for download from your web site, selling
it (either by itself or as part of some bigger software package), or
using it as the starting point for a software project of your own.

The only real limitation is that whenever you distribute SugaR in
some way, you must always include the full source code, or a pointer
to where the source code can be found. If you make any changes to the
source code, these changes must also be made available under the GPL.

For full details, read the copy of the GPL found in the file named
*Copying.txt*.

