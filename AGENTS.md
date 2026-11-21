# Working in this repository

Keep the [repository README](README.md) focused on users. Link to [contributor guidance](docs/contributor.md) for development. Use the [docs index](docs/README.md) to find the owning guide, and update that guide with changes to behavior, settings, or operating instructions.

Record reusable findings about Skyrim, SKSE, MO2, and Proton in the owning guide. Include enough evidence or a repeatable test to recover the finding in a fresh checkout.

Put implementation plans and TODOs in `docs/plans/`. Git ignores that directory. Keep durable guidance in the tracked docs.

- Change the plugin version only at the user's request.
- Treat saves and cosaves as player data. Use the [test guide](tests/README.md) for disposable fixtures and verification. Keep private saves, paths, and traces out of Git.
