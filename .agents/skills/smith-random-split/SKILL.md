---
name: smith-random-split
description: "Moving code from many key_derive calls to random::split: a key is linear (used once, &key is a use, in C too), and the three shapes of a derive: a named lane, an item index and a counter on a key in a struct. Load on 'key_derive', 'smith_key_derive' or 'smith-linear: key is used again'."
---

# From many derives to split

`key_derive(key, i)` is gone: it read a key without using it. A key is **used once**; passing it by value, taking `&key` or calling `split` on it is the use, in `.smith` and in C. `split(key, keys)` (C: `smith_key_split_n(key, keys, count)`) uses the key and gives `countof(keys)` keys, and `keys[i]` is what `key_derive(key, i)` gave, so a count's keys are the first of a larger count's. A pinned number that came from a derive by index is unchanged; the others are regenerated, and a commit says so.

## A named lane

Where a key was derived by names (`Draw_A`, `Draw_B`), make the enum 0-based with a count, split once, use each lane once:

```smith
import smith::random;

typedef enum : usize { Draw_A, Draw_B, Draw_count } Draw;

f64 pair(random::Key key) {
    random::Key lanes[Draw_count];
    random::split(key, lanes);
    return random::key_unit_double(lanes[Draw_A]) + random::key_unit_double(lanes[Draw_B]);
}
```

## An item index

One key per item: an array sized by the count (a constant bound, or an arena slice), used in the plain loop `for (usize i = 0; i < countof(keys); i++)` with `keys[i]` once in the body. The loop bound must be `countof(keys)`; another bound, an `i32` counter or an index computed from `i` is reported (`smith-lang-linear`). An item that also needs lanes splits its own key.

```smith
import smith::random;

f64 total(random::Key key) {
    random::Key items[8];
    random::split(key, items);
    f64 sum = 0;
    for (usize i = 0; i < countof(items); i++) sum += random::key_unit_double(items[i]);
    return sum;
}
```

## A counter on a key in a struct

`derive(brain->key, brain->draws++)` becomes: hold the key and replace it by one half of its split, drawing from the other.

```smith fragment
random::Key two[2];
random::split(brain->key, two);
brain->key = two[0];             // the next draw splits this one
return two[1];                   // this draw's key
```

To use one key in two places that must agree (a reference and the code under test), make it twice from a seed: `u64 seed = random::key_bits(parts[0]);`, then `random::key_make(seed)` each time. A key that a function draws from several times and a caller reads again needs a split in the caller (`random::Key parts[2];`), one part per use.

Also in this family: [smith-random](../smith-random/SKILL.md), [smith-lang-linear](../smith-lang-linear/SKILL.md).
