---
type: specification
title: HTML
description: std.html, with which a page is written as calls of plain functions, one for each element, that write the page as they run, with every text escaped, and render, which gives the page as an Html.
status: planned
---

# HTML

`std.html` writes HTML. An element is a function, a page is the calls that make it, and nothing of a page is kept as a tree: each call writes its part as it runs.

```
use std.html (render, Html, html, head, title, body, h1, p)

fn hello(name: str) -> Html {
  render() {
    html() {
      head() {
        title("Hello")
      }
      body() {
        h1("Hello, {name}")
        p("This page is written in Smith.")
      }
    }
  }
}
```

`hello("Ana")` is the page `<html><head><title>Hello</title></head><body><h1>Hello, Ana</h1><p>This page is written in Smith.</p></body></html>`.

## An element

An element is a function named for it. It is called in one of two ways.

- **With its text.** `h1("Hello")` writes `<h1>Hello</h1>`. The text is the first argument.
- **With a block.** `body() { ... }` writes `<body>`, runs the block, and writes `</body>`. The block is a lambda, written after the call as [Lambdas](../language/lambdas.md) says, and each line of it writes what stands inside the element.

A block is statements and gives no value, so what an element holds is never the value of its block. Text among other elements is written by `text`.

```
p() {
  text("Hello, ")
  b(name)
  text("!")
}
```

This writes `<p>Hello, <b>Ana</b>!</p>` for the name `Ana`. An element that holds nothing, as `br` and `input`, takes neither a text nor a block.

## Attributes

An attribute is an argument with a name, so the name of one is checked when the program is built.

```
a("Open", href: "/notes/7", class: "button")

input(name: "title", placeholder: "Title")
```

These write `<a href="/notes/7" class="button">Open</a>` and `<input name="title" placeholder="Title">`. Every element has the attributes every element may have, as `class` and `id`, and its own, as `href` on `a`. Each has a default that writes nothing, so only what is given is written. `a("Open", hreff: "/")` is refused, as `a` has no parameter `hreff`. The compiler does not build `std.html`, so no words are quoted.

An attribute the library has no parameter for, as one that begins `data-`, is written by `attr`, on the first lines of the element's block.

```
li() {
  attr("data-id", "7")
  text("First")
}
```

This writes `<li data-id="7">First</li>`.

The value is escaped, as every text is. The name cannot be escaped, since a name that holds a space or a `=` is more markup and no name. So the name is a text written in the source, and it is checked when the program is built: letters, digits, `-`, `_`, `:`, and `.`, and nothing else. `attr("data id", "7")` is refused, and so is a name that is not written out, as `attr(key, "7")`. `attr` is a [function over code](../language/code.md), which is how it reads the name where the call is written. No page is built that holds a name the library did not check.

## Every text is escaped

A text given to an element, to `text`, or to an attribute is written so that it is text and nothing else: `<`, `>`, `&`, and a quote are written as HTML writes them. `h1("{name}")` is safe whatever `name` holds, as the element escapes the finished text.

No function writes a text as HTML. What is already HTML is an `Html`, which only `render` makes, and `insert` puts one inside another page.

```
fn frame(inside: Html) -> Html {
  render() {
    div(class: "frame") {
      insert(inside)
    }
  }
}
```

## A component

A part of a page is a function that carries the effect `markup`, which every element carries. It is called where it belongs, as an element is.

```
// One note of the list.
fn note_card(note: Note) ! markup {
  article(class: "card") {
    h2(note.title)
    p(note.body)
    if note.pinned {
      span("Pinned", class: "badge")
    }
  }
}

fn notes(all: List[Note]) -> Html {
  render() {
    ul() {
      loop note in all {
        li() {
          note_card(note)
        }
      }
    }
  }
}
```

`if`, `loop`, and `match` are the language's own, and work in a page as anywhere, so there is no other language for a page that chooses or repeats.

A component that puts its caller's content somewhere takes it as a block, a lambda that carries `markup`.

```
fn layout(heading: str, inside: fn() ! markup) -> Html {
  render() {
    html() {
      head() {
        title(heading)
      }
      body() {
        h1(heading)
        inside()
      }
    }
  }
}

fn about() -> Html {
  layout("About") {
    p("Smith is a programming language.")
  }
}
```

## Where the page goes

Each element writes through the effect `markup`, and `render` is the [handler](../language/handlers.md) that answers it: it runs its block, gathers what the block writes into one text, and gives it as an `Html`. So a function that calls `render` carries no `markup`, and a component, which does not, carries it.

The operations of `markup` are private to `std.html`, as [an operation is unless it is marked `pub`](../language/effect-declarations.md#an-operation-is-private-unless-it-is-marked-pub). A program writes `! markup` and calls the elements, `text`, `attr`, and `insert`, and nothing else reaches the page, so `render` is the only handler and no function writes a text as HTML.

Nothing is sent or printed while a page is written.

## What can fail

A block gives no value, so nothing fails through one: a [`try`](../language/try.md) in a block has no function to leave. What can fail a page is done before the page is written.

```
fn note_page(id: str) -> Result[Html, str] ! alloc, store {
  note = try load(id)
  .ok(render() {
    h1(note.title)
    p(note.body)
  })
}
```

A component that meets a failure handles it where it is, and writes what stands in its place.

```
fn note_card(id: str) ! markup, store {
  match load(id) {
    .ok(note) => h2(note.title)
    .err(_) => p("This note is gone.", class: "missing")
  }
}
```

So a page is whole or is not made: no call inside `render` leaves it half written.

An `Html` is given to whatever wants a page. A route of an [HTTP server](http.md#what-a-function-gives) that gives one answers with it as a page.

An `Html` has [`Fmt`](fmt.md), which writes the page as its text. So a program prints a page, writes it to a file, or compares it in a test, as it does any value that has `Fmt`.

```
use std.console (console, print)
use std.html (render, p)

fn main() ! console, alloc {
  page = render() {
    p("a < b")
  }
  print("{page}")
}
```

This prints `<p>a &lt; b</p>`. `Fmt` reads a page and makes none: the text it gives is a `str`, and a `str` given to an element or to `text` is escaped as any text is. What is inside an `Html` stays private, so `render` remains the only thing that makes one.

## Names

A file imports each element it uses, as any name, or imports the module and writes `html.h1`, as [Qualified names](../language/qualified-names.md) says. An element whose name the file already uses, as `main` in the file that holds the program's `main`, is imported under another name with `as`.

## The forms

| Written | Is |
| --- | --- |
| `h1("Hello")` | An element that holds a text |
| `body() { ... }` | An element that holds what its block writes |
| `text("Hello")` | A text among other elements |
| `a("Open", href: "/")` | An attribute, as an argument with a name |
| `attr("data-id", "7")` | An attribute the library has no parameter for |
| `fn card(n: Note) ! markup` | A component |
| `fn layout(inside: fn() ! markup)` | A component that takes its caller's content |
| `render() { ... }` | The page its block writes, as an `Html` |
| `insert(page)` | An `Html` put inside another |
