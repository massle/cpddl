# Developer's Guide

[[_TOC_]]

## Creating a New Release
Since we never intend to have a stable API/ABI, we use only the last two
numbers of the [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

1. Make sure you are in the `master` branch.

1. Run complete tests
    ```sh
    make check-all
    ```

1. Run complete build tests
   ```sh
   ./t/scripts/test-build-apptainer.sh --git-dev master
   ./t/scripts/test-build-apptainer.sh --git-dev master --cplex /opt/cplex/cplex_studio2211.linux_x86_64.bin
    ```

1. Update `CHANGELOG.md`:
    - Change `Unreleased` to the new version and created a new `Unreleased`
        section.
    - Commit the changed CHANGELOG.md with commit message "Version X.Y"

1. Add tag
    ```sh
    git tag -a vX.Y -m "Version X.Y"
    ```

1. Push to public repo
    ```sh
    git push public master
    git push public vX.Y
    ```
