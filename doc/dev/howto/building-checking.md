# Building & checking the project {#howto-build-check}

> **Important:**
> First, make sure you've set up [all the required dependencies](@ref howto-install-dev).
> All commands on this page assume you're in the repo's root, **not** in a build folder or similar!

### Building the code
To build the project, run CMake. For a debug build:

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug && make --directory=build
```

For a release build:

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release && make --directory=build
```

### Running tests
To run all tests, only after you've built the project:

```bash
ctest --test-dir build
```

### Formatting
We use clang-format as our formatter.
Make sure you have it installed: `sudo apt install clang-format`.

Before you commit, please run our formatting script to keep the code tidy:

```bash
./tools/format-project.sh
```

If you forget to do this, the pipeline will complain.
If you would like an automated reminder, install our Git hook, which checks the code before each commit:

```bash
cp -rf tools/hooks/ .git; chmod --recursive +x .git/hooks/
```

### Linting
We use clang-tidy as our linter.
Make sure you have it installed: `sudo apt install clang-tidy`.
You can only lint after you've built the project!
To lint a specific file:

```bash
clang-tidy -p build path/to/file.cpp
```

To lint *everything*:

```bash
run-clang-tidy -p build -quiet '(cnc|common|tsnctrld)/.*'
```

Since linting takes a while to process and can sometimes be a pain to comply with,
there is no git hook for linting and the pipline will allow linting failures.

### Building the documentation on the pipeline
You could either follow the instructions below to build the documentation locally on your PC, or you could ask the CI pipeline to create a manual build for you.
The pipeline will always automatically create primary documentation builds on `main`, but not on other branches.

To create a manual documentation build, visit the
[overview of your pipeline](https://gitlab-vs.informatik.uni-stuttgart.de/software-tsn-switch/enpro-switch/-/pipelines)
and manually run the `manual-dev-docs` and/or `manual-user-docs` jobs.
When ready, your manual builds will be named based on your branch name, visit the
[GitLab pages overview](https://gitlab-vs.informatik.uni-stuttgart.de/software-tsn-switch/enpro-switch/pages#overview)
to see the links for all currently active documentation builds.
Oddities to watch out for:
- None of the links from the dev documentation to the user documentation, or vice versa, will work properly, they will always direct you to the current primary build!
  Same with the links to documentation sites in your `README.md` file.
- Since the manual builds are based on your branch name, new builds with the same branch name will overwrite old ones.
- Your manual builds will be automatically cleaned up after 1 week.


### Building the developer documentation (locally)
This command will place the generated developer documentation into a `build-dev-docs` folder in the project's root.
Inside, open the `html/index.html` file with your browser to view it.

```bash
doxygen doc/dev/Doxyfile
```

### Building the user documentation (locally)
You need Python installed to build the user documentation.
Create a virtual environment and install the necessary dependencies:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r doc/user/requirements.txt
```

After you've done that, you're already in the virtual environment and could run the next command right away.
However, when you log off and come back at a later time, you need to run `source .venv/bin/activate` again to enter the environment before you can build!
To exit the virtual environment again, simply type `deactivate`.

This command will place the generated user documentation into a `build-user-docs` folder in the project's root.
Inside, open the `html/index.html` file with your browser to view it.

```bash
sphinx-build -M html doc/user/source build-user-docs
```
