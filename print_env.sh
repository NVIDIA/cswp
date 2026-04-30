#!/usr/bin/env bash
# Copyright (c) 2023, NVIDIA CORPORATION.
# Reports relevant environment information useful for diagnosing and
# debugging __PROJECT__ issues.
# Usage:
# "./print_env.sh" - prints to stdout
# "./print_env.sh > env.txt" - prints to file "env.txt"

print_env() {
    echo "**git***"
    if [ "$(git rev-parse --is-inside-work-tree 2>/dev/null)" == "true" ]; then
    git log --decorate -n 1
    echo "**git submodules***"
    git submodule status --recursive
    else
    echo "Not inside a git repository"
    fi
    echo

    echo "***OS Information***"
    cat /etc/*-release
    uname -a
    echo

    echo "***CPU***"
    lscpu
    echo

    echo "***CMake***"
    which cmake && cmake --version
    echo

    echo "***g++***"
    which g++ && g++ --version
    echo

    echo "***gcc***"
    which gcc && gcc --version
    echo

    echo "***Environment Variables***"

    printf '%-32s: %s\n' PATH $PATH

    printf '%-32s: %s\n' LD_LIBRARY_PATH $LD_LIBRARY_PATH
}

echo "<details><summary>Click here to see environment details</summary><pre>"
echo "     "
print_env | while read -r line; do
    echo "     $line"
done
echo "</pre></details>"
