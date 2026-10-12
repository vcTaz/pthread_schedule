## Verify
`(set -o pipefail; cmake -S . -B build/tsan -DCMAKE_C_FLAGS="-Wall -Wextra -Werror -O1 -g -fsanitize=thread" -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread && cmake --build build/tsan && ./build/tsan/count_even_example | awk '{print} /count=/{c[$3]++} END{exit length(c)!=1}')`
Passing means the library and example build warning-free under ThreadSanitizer, the example runs with no reported data races, and static, dynamic and guided scheduling all produce the same count. There are no unit tests; the example is the only oracle. Output goes to the gitignored `build/tsan`.
