# Roadmap

## Baseline

- [x] Surface Map small quest-bearing location overlays
- [x] Surface Map large standalone quest markers
- [x] Native quest tracking without SWF replacement
- [x] Live Surface Map rebuild after tracking

## Candidate pull requests

- [ ] Resolve the exact inactive visible owner on mixed-active shared location
      overlays without using the aggregate active flag.
- [ ] Normalize the tested prototype to the preferred `PCH.h` and `logger::`
      project style in a no-behavior-change PR.
- [ ] Add galaxy/system-map support after independently tracing its marker
      ownership path.
- [ ] Add orbital/planet-overview support after independently tracing its
      marker ownership path.

Each map addition must remain independently reviewable and must preserve
vanilla input on every unsupported or ambiguous path.
