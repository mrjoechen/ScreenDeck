#!/usr/bin/env python3
"""Host-side behavior tests for safe LLM configuration-test statuses."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class LlmConfigTestStatusTests(unittest.TestCase):
    def test_http_failures_are_reduced_to_safe_actionable_categories(self) -> None:
        source = textwrap.dedent(
            r'''
            #include <cassert>
            #include <cstring>

            #include "llm_config_test_status.h"

            int main() {
              assert(llmConfigTestFailureForHttpStatus(401) ==
                     LlmConfigTestFailure::Authentication);
              assert(llmConfigTestFailureForHttpStatus(403) ==
                     LlmConfigTestFailure::Authentication);
              assert(llmConfigTestFailureForHttpStatus(404) ==
                     LlmConfigTestFailure::NotFound);
              assert(llmConfigTestFailureForHttpStatus(408) ==
                     LlmConfigTestFailure::Timeout);
              assert(llmConfigTestFailureForHttpStatus(429) ==
                     LlmConfigTestFailure::RateLimited);
              assert(llmConfigTestFailureForHttpStatus(502) ==
                     LlmConfigTestFailure::Upstream);
              assert(llmConfigTestFailureForHttpStatus(400) ==
                     LlmConfigTestFailure::RequestRejected);

              assert(std::strcmp(llmConfigTestFailureCode(
                                     LlmConfigTestFailure::Authentication),
                                 "authentication") == 0);
              assert(std::strcmp(llmConfigTestFailureCode(
                                     LlmConfigTestFailure::InvalidResponse),
                                 "invalid_response") == 0);
              assert(std::strcmp(llmConfigTestStateCode(
                                     LlmConfigTestState::Queued),
                                 "queued") == 0);
              assert(std::strcmp(llmConfigTestStateCode(
                                     LlmConfigTestState::Passed),
                                 "passed") == 0);
              return 0;
            }
            '''
        )
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "llm-config-test-status"
            compiled = subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "include"),
                    "-x",
                    "c++",
                    "-",
                    "-o",
                    str(executable),
                ],
                input=source,
                text=True,
                capture_output=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = subprocess.run(
                [str(executable)], capture_output=True, text=True
            )
            self.assertEqual(executed.returncode, 0, executed.stderr)

    def test_request_tokens_are_exact_nonzero_128_bit_hex_values(self) -> None:
        source = textwrap.dedent(
            r'''
            #include <cassert>
            #include <cstring>

            #include "llm_config_test_status.h"

            int main() {
              constexpr const char* LOWER =
                  "0123456789abcdef0123456789abcdef";
              constexpr const char* UPPER =
                  "0123456789ABCDEF0123456789ABCDEF";
              assert(llmConfigTestRequestTokenValid(LOWER));
              assert(llmConfigTestRequestTokenValid(UPPER));
              assert(!llmConfigTestRequestTokenValid(nullptr));
              assert(!llmConfigTestRequestTokenValid(""));
              assert(!llmConfigTestRequestTokenValid(
                  "00000000000000000000000000000000"));
              assert(!llmConfigTestRequestTokenValid(
                  "0123456789abcdef0123456789abcde"));
              assert(!llmConfigTestRequestTokenValid(
                  "0123456789abcdef0123456789abcdef0"));
              assert(!llmConfigTestRequestTokenValid(
                  "0123456789abcdef0123456789abcdeg"));

              LlmConfigTestStatus status;
              std::strcpy(status.requestToken, LOWER);
              assert(llmConfigTestRequestTokenMatches(status, LOWER));
              assert(!llmConfigTestRequestTokenMatches(status, UPPER));
              assert(!llmConfigTestRequestTokenMatches(
                  status, "00000000000000000000000000000000"));
              return 0;
            }
            '''
        )
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "llm-config-test-token"
            compiled = subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "include"),
                    "-x",
                    "c++",
                    "-",
                    "-o",
                    str(executable),
                ],
                input=source,
                text=True,
                capture_output=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = subprocess.run(
                [str(executable)], capture_output=True, text=True
            )
            self.assertEqual(executed.returncode, 0, executed.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
