---
type: specification
title: HTML
description: std.html, with which a page is written as calls of plain functions, one for each element, that write the page as they run, with every text escaped, and render, which gives the page as an Html.
status: partial
built:
  - "`std.html` declares the effect `markup`, which a page is written through as it runs, with `text` and `attr`, which write a text and an attribute's value with `<`, `>`, `&`, `\"`, and `'` written as `&lt;`, `&gt;`, `&amp;`, `&quot;`, and `&#39;` before any handler is given them"
  - "a tag stays open until something that is not an attribute is written, so `attr` follows the tag it belongs to, and a handler answers `markup` with the operations `start`, `attribute`, `emit`, `void`, and `end`"
  - "not the elements, which wait for a default of a parameter that takes a function, and not `render`, with `Html` and `insert`, which wait for a handler to append to a `Text`"
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

Nothing is sent or printed while a page is written. A function that fails halfway has made no part of a page that anyone has seen, so it gives its error as any function does.

An `Html` is given to whatever wants a page. A route of an [HTTP server](http.md#what-a-function-gives) that gives one answers with it as a page.

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
