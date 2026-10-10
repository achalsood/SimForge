# First frozen holdout evaluation, Commit 23

The corpus and recognition-file hashes were frozen in FREEZE.md before evaluation.
The first run used the renderer-disabled Debug evaluator after development tests
passed. Results: **24/28 exact positive operations and final scenarios (85.7%)**,
**20/20 correct rejections**, **zero unsafe acceptances**, and **zero accepted
silent omissions**. Mean/worst time: 91.925/208.8 microseconds.

Safe positive coverage misses, retained with their original intended semantics:

* holdout_25: Could you arrange a total of six docks?
* holdout_26: Our staff complement should become fifteen.
* holdout_27: Scale the lift truck pool to three times its present size.
* holdout_28: Please shorten the service time to twelve minutes.

The first Release evaluation reproduced these same results, with mean/worst time
8.18125/38 microseconds. Grammar was not changed to recognize the missed phrases.
The predeclared CI gate allows safe coverage misses while requiring at least 50%
exact positive matches, every negative rejected, and zero unsafe acceptances or
accepted silent omissions. This result is not an independent human-author study
or a broad-English accuracy claim. Future tuning must use a new untouched holdout.
