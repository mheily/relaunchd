## Code coverage

Here's how to generate a code coverage report:

```
cd cmake-build-debug
make -C test TestManager_coverage
open ./TestManager_coverage/index.html
```

To exclude a section of code from coverage reports,
surround the code with the following comments:

```c++
// LCOV_EXCL_START
// LCOV_EXCL_STOP
```
