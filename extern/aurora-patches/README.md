# aurora patches

Applied in filename order by `extern/aurora.cmake` (FetchContent `PATCH_COMMAND`) on top of the pinned
aurora commit. Each patch is a plain `git diff` against that commit; keep them small and explain the
reason at the top of each file. Changing a patch requires a reconfigure (`cmake -S . -B build/cmake`).
