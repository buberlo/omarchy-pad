Build 4451's first actual-game diagnostic supersedes static FPCR reachability as the optimization priority. `reports/ss4451-hollow-startup-1.txt` has two main-thread rings of 64 records each: every record is an LDAPUR/STLUR-family instruction, and none is MSR FPCR. All 1,223 retained records across contexts have that same family classification (some worker records repeat between snapshots).

The FPCR candidate remains unapplied. Its successful local semantics tests do not establish that it would improve Hollow Knight. The next controlled test is the source-verified `FEX_HOSTFEATURES=disablelrcpc2` override described in `../rcpc2-audit/README.md`, preserving memory ordering through FEX's existing LDAPR/STLR path.

An independent source review by the graphics agent found no new FPCR candidate blocker; it agreed that full dispatcher/syscall/fallback/thread tests remain required if the candidate is reconsidered. That review is not a reason to activate it without frequency evidence.
