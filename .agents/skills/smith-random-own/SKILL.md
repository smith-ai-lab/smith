---
name: smith-random-own
description: "Writing your own distribution on a random::Key: taking bits, splitting a key for its parts, keeping it reproducible. Load when the built-in distributions do not fit."
---

# Write your own distribution

A distribution is a function of a key built only from the public primitives, written exactly like smith's. **Map the zero word to the simplest value, and let values move away from it as the word grows**, so a failing test shrinks to it: smith's test runner records the words a test drew and makes them smaller as unsigned integers. `random::key_unit_double(key)` is 0.0 for the word 0, so an exponential drawn as below gives 0 for it, the simplest value. A mixture splits its key for each part, so the parts are independent and each key is used once:

```smith
import smith::random;

f64 exponential_sample(random::Key key, f64 rate) {
    return -random::log_double(1.0 - random::key_unit_double(key)) / rate;
}

f64 mixture_sample(random::Key key, f64 rate, f64 mean, f64 deviation, f64 weight) {
    random::Key parts[3];
    random::split(key, parts);
    if (random::bernoulli_sample(parts[0], (random::Bernoulli){.probability = weight}))
        return exponential_sample(parts[1], rate);
    return random::normal_double_sample(parts[2], (random::NormalDouble){.mean = mean, .deviation = deviation});
}
```

Also in this family: [smith-random](../smith-random/SKILL.md), [smith-random-distributions](../smith-random-distributions/SKILL.md), [smith-random-c](../smith-random-c/SKILL.md).
