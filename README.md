# Lispico Confetti

An experimental alternative frontend for
[Lispico](https://github.com/unclechu/lispico),
using [Confetti](https://github.com/hgs3/confetti) for parsing. Confetti’s
native directive and subdirective AST is translated into Lispico expressions and
evaluated by the original interpreter and standard library.

This frontend uses **Confetti’s native `{ … }` blocks exclusively** for nested
expressions. It does not enable Confetti’s parenthesized-expression extension,
and it does not tokenize or parse expression strings independently.

See [examples](examples) for runnable programs,
[examples/ported-lispico-examples](examples/ported-lispico-examples) for all
original Lispico examples ported to Confetti syntax, and
[examples/fail](examples/fail) for expected translation failures.

## Relationship to Lispico

Lispico Confetti is an alternative **frontend**, not another implementation
of Lispico’s runtime. Its standard library, value types, error behavior,
variable and array semantics, allocation limits, and memory management
(including manual garbage collection) come from Lispico itself.

**[Lispico](https://github.com/unclechu/lispico) is still in active
development.** Its features and behavior will continue to evolve. When this
project updates its pinned Lispico dependency, new runtime capabilities will
become available here too, although some may need additional translation rules.

For the complete feature set and runtime documentation, consult
**[Lispico’s README](https://github.com/unclechu/lispico#readme)**. This README
concentrates on expressing Lispico programs using Confetti syntax.

## Build and run

For a local build, initialize the Git submodules first:

```sh
git submodule update --init --recursive
./build.sh
./lispico-confetti examples/hello.confetti
./lispico-confetti examples/blocks.confetti
./lispico-confetti examples/arrays.confetti
./lispico-confetti examples/infix.confetti
./lispico-confetti examples/prefix.confetti
./lispico-confetti examples/strings.confetti
./tests/run-examples.sh
```

Or use Nix with pinned Lispico and Confetti sources:

```sh
nix-build -A lispico-confetti
./result/bin/lispico-confetti examples/hello.confetti
```

To use locally initialized submodules instead of the pins:

```sh
nix-build -A lispico-confetti --arg useLocalSubmodules true
```

## Translation rules

Confetti supplies the complete structural AST. Lispico Confetti interprets
directives and their arguments as Lispico expressions:

| Confetti                  | Lispico              | Meaning                                       |
| ---                       | ---                  | ---                                           |
| `message`                 | `(message)`          | Call a function without arguments             |
| `id message`              | `message`            | Use a symbol’s value                          |
| `id 123`                  | `123`                | Use a literal value                           |
| `println message`         | `(println message)`  | Call a function with an argument              |
| `var "x" {gc}`            | `(var "x" (gc))`     | A block supplies a call argument              |
| `str {id "a"; id "b"}`    | `(str "a" "b")`      | Block subdirectives supply separate arguments |
| `str "a" {id "b"}`        | `(str "a" "b")`      | Block subdirectives supply separate arguments |
| `str "a" "b"`             | `(str "a" "b")`      | Plain arguments                               |
| `str {}`                  | `(str)`              | Empty block supplies no arguments             |
| `str`                     | `(str)`              | Empty block supplies no arguments             |
| `str do {id "a"; id "b"}` | `(str (do "a" "b"))` | Explicit sequence is one argument             |
| `id {gc}`                 | `(gc)`               | Pass the result of a nested call through      |
| `1 + 2`                   | `(+ 1 2)`            | Infix operator directive                      |
| `+ 1 2`                   | `(+ 1 2)`            | Ordinary prefix operator directive            |
| `1 + 2 + 3`               | `(+ 1 2 3)`          | Chain the same arithmetic operator            |
| `1 && 0 && 1`             | `(and 1 0 1)`        | Short-circuit logical AND                     |
| `0 \|\| 1 \|\| 0`         | `(or 0 1 0)`         | Short-circuit logical OR                      |
| `do { … }`                | `(do …)`             | Explicit sequence                             |

Quoted arguments represent string literals, while bare names represent
symbol references. Numeric arguments use Lispico’s existing numeric parser.
For example:

```confetti
var "name" "World"
println name
println "name"
```

```lisp
(var "name" "World")
(println name)
(println "name")
```

### Native argument blocks and infix operators

An ordinary block supplies **zero or more arguments** to the enclosing
function, one for each logical expression inside it. The block is not
implicitly grouped into a `(do)`. For example:

```confetti
var "joined" {str {str "Hello"; str " world"}}
assert {joined == "Hello world"}

var "shorter" {str "Hello" " world"}
assert {shorter == joined}

var "empty" {str}
assert {empty == ""}
```

```lisp
(var "joined" (str (str "Hello") (str " world")))
(assert (== joined "Hello world"))
(var "shorter" (str "Hello" " world"))
(assert (== shorter joined))
(var "empty" (str))
(assert (== empty ""))
```

Literals and variable references inside argument blocks still use `id`, the
identity special form. It passes its operand through without an additional
Lispico call, like an identity function.

For example, `str {id "Hello"}` is supported, but `str {"Hello"}`
does not implicitly convert its quoted-only subdirective into a value.
Likewise, `id message` passes the variable’s contents, while
`message` is a zero-argument function call.

The preferred operator syntax is infix:

```confetti
var "answer" {1 + {2 * 3}}
assert {answer == 7}
var "difference" {20 - 5 - 3}
assert {difference == 12}
```

```lisp
(var "answer" (+ 1 (* 2 3)))
(assert (== answer 7))
(var "difference" (- 20 5 3))
(assert (== difference 12))
```

There is **no operator precedence**. Each directive may use only one kind of
infix operator. Parent-child relationships in Confetti’s AST express nesting:

```confetti
# Accepted: explicit grouping
var "number" {1 + {2 * 3}}

# Rejected: mixed operators without grouping
# var "number" {1 + 2 * 3}
```

Arithmetic `+`, `-`, `*`, and `/` and logical `&&` and `||` can be chained left
to right. Comparisons `<`, `<=`, `>`, `>=`, `==`, and `!=` accept two operands
only. A long chain is split into left-associated Lispico calls when it exceeds
Lispico’s native argument capacity.

### Logical operators and short-circuiting

`&&` and `||` are translated directly to Lispico’s existing `(and)` and
`(or)` special forms. Unlike ordinary function calls, these forms stop
evaluating arguments when the result is already known:

```confetti
var "enabled" {1 && {0 || 1}}
assert {enabled == 1}

# These undefined calls are never evaluated.
assert {id {0 && {never-called}} == 0}
assert {id {1 || {never-called}} == 1}

# Prefix notation works too.
assert {id {&& 1 1} == 1}
assert {id {|| 0 1} == 1}
```

```lisp
(var "enabled" (and 1 (or 0 1)))
(assert (== enabled 1))
(assert (== (and 0 (never-called)) 0))
(assert (== (or 1 (never-called)) 1))
(assert (== (and 1 1) 1))
(assert (== (or 0 1) 1))
```

As with arithmetic, there is no implicit precedence or mixing of infix
operators. `1 && 0 || 1` is invalid: use `1 && {0 || 1}` or
`id {1 && 0} || 1` inside a block, depending on the intended grouping.
See [the logical example](examples/logical.confetti) for executable tests,
including nested expressions and long chains.

Infix directives work at the top level too, including with Lispico’s
`last-val` function:

```confetti
1 + 2
last-val
```

This returns `TYPE_U32(3)`. The regression examples also cover top-level
`id {1 + 2}`, composed `id` expressions, and long arithmetic chains.

Prefix calls are still supported: they are just ordinary Confetti directives.
The [prefix example](examples/prefix.confetti) demonstrates both forms:

```confetti
var "two" {+ 1 2}
var "six" {+ 1 2 3}
```

```lisp
(var "two" (+ 1 2))
(var "six" (+ 1 2 3))
```

### `id` and neighboring operator directives

Confetti allows a new directive immediately after another directive’s closing
brace. The translator can therefore combine an `id` directive or an ordinary
function call with a trailing argument block with the following operator
directives inside the same native block:

```confetti
var "numbers" {arr 10 20 30}
assert {id {arr-get "numbers" 1} == 20}
assert {type-of {3 - 2} == u32}
assert {id {2 + 3} == id 5}
sin {pi / 2.0} - 1.0
```

```lisp
(var "numbers" (arr 10 20 30))
(assert (== (arr-get "numbers" 1) 20))
(assert (== (type-of (- 3 2)) u32))
(assert (== (+ 2 3) 5))
(- (sin (/ pi 2.0)) 1.0)
```

Confetti provides neighboring subdirectives, not an opaque expression string:

```text
assert
├── id
│   └── arr-get "numbers" 1
└── == 20
```

Here `id {arr-get "numbers" 1}` provides the left operand, and `== 20`
continues the expression. The outer `id` is unnecessary when the left side
is already a function call with an argument block, and `id` may optionally
introduce the right operand as well. Subsequent operator continuations must
use the same operator. This is an interpretation of Confetti’s native AST; no
extra lexical scanning is involved.

Arithmetic continuations work after any function call with a trailing block,
not just `id`. For example, `sin {pi / 2.0} - 1.0` subtracts from the result
of `sin`, whereas a complete prefix call such as `+ 1 2 3` remains a separate
directive. The translator distinguishes these cases using Confetti’s native
operator and operand arguments.

A named function call with inline arguments can also precede a comparison
within one directive:

```confetti
assert {div 7.5 2.0 == 3.0}
assert {div {id 7.5; id 2.0} == 3.0}
```

Both translate to `(assert (== (div 7.5 2.0) 3.0))`. In the second example,
the `id` markers are needed because native Confetti subdirectives do not
interpret `7.5 2.0` as two separate literal arguments.

The right-hand operand can likewise be a function call with inline arguments:

```confetti
var "signed" {arr {type i32 0}}
assert {arr-get "signed" 0 == type i32 0}
assert {arr-get "signed" 0 == {type i32 0}}
```

Both assertions compare `(arr-get "signed" 0)` with `(type i32 0)`.
Between infix operators, one native argument remains an atom, while multiple
arguments form a function call. For example, `1 + type u32 2 + 3` translates
to `(+ 1 (type u32 2) 3)`. Operators still cannot be mixed without braces,
and `id` still introduces exactly one explicit operand. No new lexer or
source-level grouping is needed: these are Confetti’s existing arguments.
The final call may also use a native argument block, as in
`assert {1 == type u32 {id 1}}`; quoted operator spellings remain ordinary
arguments rather than splitting the call.

One logical argument can span multiple directives, while a subsequent
`id` starts a new argument:

```confetti
assert {
  id {str-size "Hello"} == 5
  id "Expected exactly five bytes"
}
```

```lisp
(assert (== (str-size "Hello") 5) "Expected exactly five bytes")
```

Nested calls may also appear as the **last** operand without `id`:

```confetti
assert {32 == {2 + {arr-get "numbers" 2}}}
```

```lisp
(assert (== 32 (+ 2 (arr-get "numbers" 2))))
```

An anonymous `{ … }` cannot appear at the start of a Confetti directive.
Consequently, `assert {{arr-get "numbers" 1} == 20}` is not valid Confetti;
use the explicit `id` form above.

### Sequences and expression values

`do` creates a sequence, evaluating its expressions in order and returning the
last result. It also establishes Lispico’s existing sequence control-flow
boundary. An ordinary function block expands its expressions as separate
arguments; **explicit `do` is required to pass a sequence as one argument**.

```confetti
var "answer" do {
  var "first" 10
  first + 5
}

do {
  println "Computed a result"
  id answer
}
```

```lisp
(var "answer" (do (var "first" 10) (+ first 5)))
(do (println "Computed a result") answer)
```

`id answer` is important when the final sequence expression should be a
symbol reference rather than a zero-argument function call. An explicit
`id {call}` also works when the expression is a nested call.

An empty ordinary block supplies no arguments; an explicit empty sequence
supplies one argument if passed to another function:

```confetti
str {}    # Equivalent to (str)
str       # Equivalent to (str)
str do {} # Equivalent to (str (do); Lispico will fail to evaluate)
do {}     # Equivalent to (do)
do        # Equivalent to (do)
```

Conditional branches are intentionally different: an `if` branch still
requires exactly one expression, or an explicit `do` for an empty or
multi-expression sequence. This prevents a second expression from becoming
an accidental false branch.

### Strings, escapes, and NUL bytes

Confetti first processes its own escaping (a backslash escapes the following
character). Lispico Confetti then decodes surviving backslash sequences using
Lispico’s string literal rules. This applies to ordinary and triple-quoted
arguments, including inside native `{ … }` blocks.

For instance, two backslashes in Confetti source preserve one backslash for
the second stage:

```confetti
var "message" "hello\\nworld"
var "binary" "A\\0B"
var "literal" "hello\\\\nworld"
```

```lisp
(var "message" "hello\nworld")
(var "binary" "A\0B")
(var "literal" "hello\\nworld")
```

| Confetti source | Result                            |
| ---             | ---                               |
| `\\n`           | Newline (LF)                      |
| `\\r`           | Carriage return (CR)              |
| `\\t`           | Tab                               |
| `\\0`           | NUL byte                          |
| `\\\\`          | Literal backslash                 |
| `\\\\n`         | Literal backslash followed by `n` |
| `\\\\0`         | Literal backslash followed by `0` |

Confetti handles ordinary escaped quotes such as `\"`. The second decoder
also recognizes Lispico’s quote escape when it survives Confetti’s first
pass. Unknown or incomplete surviving escapes cause translation errors.

For example, Confetti decodes `"\n"` to just `"n"`, which is **not** a
newline. Conversely, `"\\q"` fails during Lispico decoding, while
`"\\\\q"` represents a literal backslash followed by `q`.

Triple-quoted strings contain real line breaks and undergo the same second
decoding stage. Indentation inside the quotes is part of their value:

```confetti
var "message" """First line
Second line"""
assert {id {str-get "message" 10} == 10}
```

Lispico strings store explicit byte lengths, so an embedded NUL does not
truncate the value. A final terminator is stored separately.

### Conditionals

The translator supports `if`, `else if`, and `else`. Each branch is evaluated
only when selected, preserving Lispico’s ordinary lazy `if` behavior.
Conditions may be single values or infix comparisons:

```confetti
if 5 > 2 {
  println "bigger"
} else if 5 < 2 {
  println "smaller"
} else {
  println "equal"
}
```

Equivalent Lispico:

```lisp
(if (> 5 2)
  (println "bigger")
  (if (< 5 2)
    (println "smaller")
    (println "equal")))
```

`then` is optional when the condition fits in the directive’s argument list:

```confetti
if enabled {
  println "Enabled"
}
if enabled then {
  println "Enabled"
}
```

However, **Confetti permits only one trailing block per directive**.
If the condition itself needs a block, the branch must be an adjacent
`then` or `do` directive:

```confetti
if {5 > 2} then {
  println "bigger"
} else if {5 < 2} then {
  println "smaller"
} else {
  println "equal"
}
```

This is still entirely native Confetti AST: `if {5 > 2}` contains the condition
block, and the next sibling `then { … }` supplies its branch. No embedded
expression strings are re-parsed. Writing `if {condition} {branch}` directly
is invalid because the second block lacks its own directive.

A regular branch block must contain **exactly one expression**. Use `do` for
zero or multiple expressions; optionally combine it with `then`:

```confetti
if {enabled} then {
  println "Enabled"
} else do {
  println "Disabled"
  println "Try enabling it"
}

if enabled then do {
  println "First"
  println "Second"
}

if enabled do {}
```

`do` wraps its contents in Lispico `(do …)`. For example, the first conditional
above corresponds to:

```lisp
(if (enabled)
  (println "Enabled")
  (do
    (println "Disabled")
    (println "Try enabling it")))
```

To read the value of the variable `enabled`, use `if enabled` rather than
`if {enabled}`: the braces make `{enabled}` a zero-argument function call,
equivalent to Lispico `(enabled)`. Inside a condition block, use
`{id enabled}` if a variable reference must be explicitly grouped.

See `examples/conditionals.confetti` and `examples/conditionals-root.confetti`
for executable examples of both condition forms, sequences, and chains.

## Development environment

### [direnv](https://direnv.net) example

Create `.envrc`:

```sh
use nix --arg useLocalSubmodules true
```

### Local Vim configuration example

```vim
se cc=81,101 tw=100

" Prevent *.h files from being detected as C++
augroup force_c_headers
  autocmd!
  autocmd BufReadPost,BufNewFile *.h setlocal filetype=c
augroup END

" Use Nginx syntax highlighting for Confetti files
augroup force_confetti_syntax_hl
  autocmd!
  autocmd BufReadPost,BufNewFile *.confetti setlocal filetype=nginx
augroup END

let g:ale_linters_explicit = 1
let g:ale_linters = {'c': ['cc'], 'sh': ['shellcheck']}
let g:ale_c_cc_executable = 'gcc'
let g:ale_c_cc_options = '-std=c23 -Wall -Wextra -Wpedantic -Wno-pragma-once-outside-header'

" GCC checker that uses the source file’s directory as its working directory.
" For correct resolution of included headers with relative paths.
call ale#linter#Define('c', {
\ 'name': 'lispico_confetti_cc',
\ 'output_stream': 'stderr',
\ 'executable': function('ale_linters#c#cc#GetExecutable'),
\ 'command': {b -> ale#c#RunMakeCommand(b, function('ale_linters#c#cc#GetCommand'))},
\ 'cwd': {b -> fnamemodify(bufname(b), ':p:h')},
\ 'callback': 'ale#handlers#gcc#HandleGCCFormatWithIncludes',
\})

" Use this checker only for Lispico Confetti’s own C sources.
augroup lispico_confetti_ale
  autocmd!
  autocmd BufReadPost,BufNewFile */lispico-confetti/src/*.c
    \ let b:ale_linters = ['lispico_confetti_cc']
augroup END
```

## TODO

- `i = 0` pseudo-function alias for `set "i" 0`
- `foo[n]` pseudo-function alias for `arr-get "foo" n`
- `foo[n] = x` pseudo-function alias for `arr-set "foo" n x`
- `$foo[n]` pseudo-function alias for `str-get "foo" n`
- `$foo[n] = x` pseudo-function alias for `str-set "foo" n x`

## License

SPDX: MIT

All files in this repository (apart from third-party dependencies) are
licensed under [MIT](LICENSE).
