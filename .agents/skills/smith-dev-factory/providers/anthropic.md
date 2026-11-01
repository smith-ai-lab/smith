---
name: anthropic
description: The Anthropic provider of the smith factory: which model each ticket difficulty runs on, how runners are launched, and Anthropic's model-choice guidance.
---

# Anthropic provider

The only file of the factory that names models (#1409). Tickets name difficulties; this file maps them to models.

## Mapping
| Difficulty | Model | Effort |
|---|---|---|
| `difficulty:easy` | Haiku 5.5, `claude-haiku-5-5` | medium |
| `difficulty:medium` | Sonnet 5.5, `claude-sonnet-5-5` | medium |
| `difficulty:hard` | Sonnet 5.5, `claude-sonnet-5-5` | high |

Opus is never in the mapping. Only the lead assigns it, per ticket, and the daily review on #73 reports every use.

## Launching a runner
- **Cloud:** a session tagged `smith-runner`, permission mode `auto`, with the prompt `You are a smith runner. Load the skill smith-dev-runner and follow it.` Its model is the mapping's model for the ticket's difficulty.
- **Mac:** the `claude` CLI in the repository checkout, with the API key read from 1Password through `apiKeyHelper` in `.claude/settings.local.json`. That file holds only the helper; the key is never printed and never committed. Same prompt, same model mapping.

## Model choice guidance
Not yet summarized. The fetch of https://docs.anthropic.com/en/docs/about-claude/models/choosing-a-model failed: the host does not resolve from the session that wrote this file (its network domains do not allow it). Fill this section in from that page once it can be read; until then, do not rely on any summary here.
