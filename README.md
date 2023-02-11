# Chord

Implemented the Chord protocol in a team of two.

http://www.cs.berkeley.edu/~istoica/papers/2003/chord-ton.pdf

My teammate and I used Google protobuf structures and sha1 to build Chord on top of TCP. Chord is a scalable peer-to-peer lookup protocol for internet applications.

The main driver code is located in chord.h. Utility functions are in a4protos.h and chord_functions.h

### Usage

Run `make` to compile.

Run `chord -p <port number> --sp <stabilize in seconds> --ffp <fix_fingers in seconds> --cpp <check_pred in seconds> -r <num succ>` to start a new chord ring.

To join an existing ring add the `--ja <IPv4 address>` and `--jp <existing port number>` arguments.
