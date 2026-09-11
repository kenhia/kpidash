# 019 — the warning gate that can reach the dashboard sources, and the exec-stack warning underneath it

Work item: `korg:2307`, the residual half of WI 1934, filed by sprint 018.
Run from cleo against kai, 2026-09-11, at Ken's go-ahead on the one thing that
needed him: an apt install.

One item, two findings that shared a build — which is why 018 filed them
together rather than separately, and that judgement paid off.

## The problem 018 was honest about

Sprint 018 added `just check-release`: the tested sources recompiled at
`-O2 -Werror`, so that GCC's flow-sensitive warnings — the ones that only run
once the optimiser does — are gated rather than noticed by eye in deploy
output.

It worked, and it could not have caught the warnings it was written for.

`check-release` configures `-DTESTS_ONLY=ON`, which compiles five files:
`config.c`, `redis.c`, `registry.c`, `icons.c`, `memstat.c`. The two warnings
from WI 1934 were in `fortune.c` and `ui.c` — dashboard-only sources, not one
of the five. The gate was pointed away from its own motivating case.

It was pointed there for a reason rather than by mistake: compiling the
dashboard sources needs a full native configure, which needs LVGL **and
libpng**, and kai had no `libpng-dev`. `cmake -B build-native` could not
configure at all. So 018 could not fix it, and instead of leaving `CLAUDE.md`
asserting something false it wrote down exactly what was and was not gated.
That is why this sprint had a precise target instead of a rumour.

## What changed

**1. `libpng-dev` on kai.** A machine change, so Ken's call — asked, approved,
installed, recorded as a k-homelab fold-in (`korg:2383`). `libpng-dev` and
`libpng16-16t64`, both `1.6.43-5ubuntu0.6`. Build-time only: no service, no
config, nothing listening. kai already had `libdrm`, `libcjson` and `hiredis`,
so libpng was the single gap.

**2. `just check-warnings-full`.** A native Release configure over the *whole*
tree at `-O2 -Wall -Wextra -Werror` — `main.c`, `ui.c`, `fortune.c`,
`screenshot.c`, `src/widgets/*` included.

Deliberately **not** in `just check`. It needs `lib/lvgl` initialised, which is
the big submodule the tests-only gate has always been able to skip, and it
needs libpng on the host. Making `just check` depend on both would trade a
one-command clean-clone gate for a warning class that a pre-deploy run catches
just as well. The recipe fails with a readable message naming `libpng-dev`
rather than a cmake configure error, so a host without it is told what to do.

**The tree was already clean under it.** Zero warnings at `-O2` across every
source, first run. That is the good outcome — it means the gate goes in with
`-Werror` live today rather than with a backlog to burn down first — but it is
worth saying plainly that the gate found no new compiler warnings. Its value
is forward-looking.

**3. The exec-stack warning: a nested function in `redis.c`.** `ld` warned on
every link that `redis.c.o` "requires executable stack (because the
`.note.GNU-stack` section is executable)". Pre-existing, confirmed on `main`
by 018, so not from that sprint.

The cause, found by comparing object files rather than guessing: in
`build-pi5`, `redis.c.o`'s `.note.GNU-stack` carried the `X` flag while
`registry.c.o` and the test objects in the same directory — same compiler, same
flags — did not. That narrows it to something in the file, and the something
was `repo_cmp`, a `qsort` comparator defined **inside** `poll_repos`. A GCC
nested function needs a trampoline, the trampoline lives on the stack, and GCC
duly marks the stack executable.

It captures nothing from the enclosing scope, so it moved to file scope as a
`static`. Behaviour-identical; the comment records why it must stay there.

## Verification

- `just check-warnings-full` — exit 0, **0 warnings** over the whole tree.
- `just check` — exit 0. 80 client tests pass, native unit tests pass.
- Cross-build to aarch64 via `cmake/aarch64-toolchain.cmake` — exit 0, **0**
  exec-stack warnings (was: one on every link).
- Object-level proof, `redis.c.o` in the cross build:

  ```
  before:  .note.GNU-stack   PROGBITS  ...  X
  after:   .note.GNU-stack   PROGBITS  ...
  ```

- And the part that is more than cosmetic — the shipped Pi binary:

  ```
  before:  GNU_STACK  ...  RWE
  after:   GNU_STACK  ...  RW
  ```

  `kpidash` on the Pi no longer requests an executable stack. The warning was
  reporting a real, if small, hardening regression, not just noise.

## The caveat that replaces the old one, and why it matters

The honest limitation has moved rather than disappeared, and `CLAUDE.md` now
says so: **neither gate sees linker warnings.**

This sprint is its own proof. The native x86_64 build links perfectly clean —
no exec-stack warning, and `redis.c.o` has no `X` flag there either, because on
x86_64 GCC 13 does not need the trampoline for this comparator. The warning
exists only on aarch64, i.e. only in the build that actually ships. A native
gate, however thorough at compiling, is structurally blind to it.

So: **compiler** warnings across the whole tree are now gated; **linker**
output at deploy time is still read by eye. That is a smaller uncovered class
than before and a more specific one — the value is in it being named, so it
does not quietly widen back out to "read the deploy output" for everything.

## Repaired in passing

Nothing separate. Both findings were in scope on the item.

## Not done

- No new build tree is committed; `build-*/` stays git-ignored and the
  throwaway verification trees were removed.
- `clang-format` and `cppcheck` are still not installed on kai and still not in
  the gate. Unchanged by this sprint and not in its scope.
