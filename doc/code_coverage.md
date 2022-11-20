## Code coverage

Here's how to generate a code coverage report:

```
./create-coverage-report.sh
```

To exclude a section of code from coverage reports,
surround the code with the following comments:

```c++
// LCOV_EXCL_START
// LCOV_EXCL_STOP
```
