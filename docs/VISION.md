# Vision

Kept next to `DECISIONS.md` (from the 2026-10-08 review) until the owner decides whether both
are worth keeping; where they differ, `DECISIONS.md` wins. Written in the old `3dgpuanimation`
repo; paths such as `index.html` (the descent) and `home.html` refer to that repo.

What the owner wants from this project, taken from his own messages in the session where the
engine was built (54 typed messages, 2026-10-06 → 2026-10-07, numbered `#1`–`#54` in order,
times UTC). Quotes are verbatim, typos included. Statuses were checked against the code and
`docs/STATE.md` on 2026-10-08. Where he changed his mind, the latest wins and the earlier view
is noted.

## The idea in his words

> "i want more of an engine that acan put up those already-made boxes(blender 3d obj files and
> stuff that usually 3dprints and stuff use but with custom shapes for example too)" — #1

> "can we also do this test site not one particular 3d but the further idea of "3d cutter
> engine" or something?" — #17

> "in the end result in that explorer i wanna see something that resolves my ask - can we do
> that as some manual engine to produce theese kinds of websites of exploring some processess on
> fucking PCB of videocard or whatever" — #40

> "uy must follow the original plan of making an engine that can create thing like oleg's
> design provide(look the real web-0page content how it looks works etc and implement that in
> way that we can build like separate "engine" application to create theese disassembly things"
> — #46

In short: the GPU → Atom descent is the look to match; the product is an engine (and later an
application) that produces descents like it for any device.

## Wants by topic

Status: **done**, **partial**, **not started**, **changed his mind**.

### Look and design

| #        | Want                                                                                                                                                                                                                                              | Status                                                                                                                                                        |
| -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| #22, #32 | Look like the descent: "see how his one is pleasant and good its more like web-design perfect, we wanna try to explore ghis way of doing it how is that achieved" (#32); "copy whats in his thing but make compatable with our engine idea" (#22) | **partial** — viewer uses the descent's design language; remaining gaps listed in STATE "Looks"                                                               |
| #29      | "currently still kinda looks like some game on old android, can we get some textures or something to highgen graphix aspect or nah?"                                                                                                              | **partial** — painted surfaces (pcb, traces, laminate, carbon, brushed) and die patterns; no photo-based textures                                             |
| #40      | "theese all are just one tesxture, no detail at all, maybe reference some photos or ask me to get like cropped pictures of stuff or website, we should find a way to make it look good not like 45 squares"                                       | **partial** — procedural detail added; reference photos never requested from him                                                                              |
| #45      | "not just a box but some texture-like for input DP for example and so on so like it looks better"                                                                                                                                                 | **done** — `port` shape, connector receptacles                                                                                                                |
| #46      | "so is the looks applied only to 1 surface ? fix that rn"                                                                                                                                                                                         | **done** — surfaces painted on both faces (holed slabs still get none, STATE)                                                                                 |
| #40      | "also see how finely it splits and looks here"                                                                                                                                                                                                    | **partial** — Disassemble with make-room; heat pipes / fin stacks simpler than the descent's                                                                  |
| #35      | "darker pcb u meant black xd?"                                                                                                                                                                                                                    | unclear — quoted as is                                                                                                                                        |
| #45      | "i see that in cut dont like that" (screenshot)                                                                                                                                                                                                   | unclear what he disliked; Cut has hatched caps                                                                                                                |
| #18–#19  | "i have incorrect how labels show and also its kinda too fast"; "at some point it was like stacked on itself"                                                                                                                                     | **partial** — engine Labels mode keeps tags apart; the same bugs in the descent itself (overlapping labels, fast pacing) are listed in STATE and left unfixed |
| #2       | "make text more readable maybe idk imma tell the design aspects when we test stuff"                                                                                                                                                               | open — waiting on his design notes                                                                                                                            |

### Models and levels

| #        | Want                                                                                                                                                                                                                    | Status                                                                                                                                           |
| -------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| #1, #2   | Load ready-made files: "blender 3d obj files and stuff that usually 3dprints"; on formats: "easier one (one that most people use? why no both?)"                                                                        | **partial** — STL, GLB, glTF load (`src/models/load.ts`); no OBJ or 3MF                                                                          |
| #1       | "with custom shapes for example too"                                                                                                                                                                                    | **done** — box / cylinder / sphere / fan / port / outline shapes in data                                                                         |
| #9       | "can we create yeah some like generig card and show layers and stuff like that(like in his, but make it more flexible like i said?)"                                                                                    | **done** — layered assemblies; the generic demo card was later replaced by the RTX 5070 Ti                                                       |
| #24      | "maybe make it layered, so if a certain part is chosen it puts THAT parts and produces the 3d of this thing to examine, and inside it and so fiorth"                                                                    | **done** — dives along `detail` links, breadcrumb, Back                                                                                          |
| #28      | "lets take another card's full sheet and try to make our own some of theese files youknow and transfer it accordingly to this demo like make our own with an engine"                                                    | **done** — RTX 5070 Ti card → PCB → GB203 package → die, with a tour                                                                             |
| #33      | "make like iphone 5, google pixel 6, radiomaster pocket ... and so on"                                                                                                                                                  | **partial** — Pixel 6 kept; iPhone 5 and RadioMaster Pocket were built and then removed at his request (confirmed in the 2026-10-08 review)      |
| #34, #49 | Drone first: "drone mainly try to explore" (#34); "find some datasheet(of some drone for example) and transfer ALL elements you can find, try t o improve their looks(add texctures/stuff like that), add logick" (#49) | **partial** — quad tour (quad → FC → MCU, plus ESC, motor, battery); the quad is rough boxes from a script, not researched dimensions (PLAN 1.1) |
| #41      | "do a real datasheet info displayed, dont imagine stuff or assume how it works"                                                                                                                                         | **partial** — specs carry sources; drone part positions not yet checked against photos (PLAN 1.2)                                                |
| #40      | "later add some arduino chip and do visualisation of how stuff there works(all chips elements input outputs current and so on) as a separate task(my version of it, with oleg's looks)"                                 | **not started** — PLAN 3.3                                                                                                                       |

### Links and flows

| #   | Want                                                                                                                                                                     | Status                                                                                                                  |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------- |
| #29 | "see how hes got all that data animations and stuff, we'd wanna add those too later when just view is good"                                                              | **partial** — links with pulses, role-based flow presets (`gpu-board`, `gpu-die`, `fpv-quad`); no per-chip choreography |
| #40 | "here in pcb itself theres some additional cool data and some kind of visual interpretation of whats hjappening on his card, can we do something like this on our shit?" | **partial** — same as above; no Thermal or Trace/Follow views                                                           |

### Animations and navigation

| #             | Want                                                                                                                                                                                                                                                 | Status                                                                                                       |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| #23           | "i want that disassembly process like his not just this explore mode"                                                                                                                                                                                | **done** — Disassemble slider with make-room                                                                 |
| #30, #34, #43 | "add like arows and auto-mode where it just tabs thropugh longest way of zoom-ins yk?" (#30); corrected in #34: "NOPOOOO zoom in i meant see how his shit is lkike ... _> ARROW ... ->ARROW ... consider that our goal"                              | **done** — arrows and Auto along `detail` links or a tour, story mode with HUD                               |
| #49           | "diffrent zoom-in(like dive-in explore thingy thats on arrows) for motor/esc/fc/stack stuff like that"                                                                                                                                               | **partial** — FC, MCU, ESC, motor, battery levels exist; stack layout approximate                            |
| #35           | "dive in currently has no animation, i want it to transition smoothly like do something like zooming in with fades or idk somehow and show ill tell ya"                                                                                              | **partial** — fly-in / fade / arrive transitions; not one continuous camera move like the descent (PLAN 1.3) |
| #20           | "what if all that video translation to smaller atom was pre-calculated, took some time on gpu while tpe 3d overview loaded fast ... but the transiotions were animated ... what u think bout integrating that, or i dont understand somethign there" | **not started** — an open question, not a decision                                                           |

### Editor and GUI

| #   | Want                                                                                                   | Status                                                                                                                            |
| --- | ------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------- |
| #17 | "make that page in similiar style to that 3d viewer a mnenu that displays loaded 3d models basically"  | **done** — model list in the viewer                                                                                               |
| #49 | "the menu is gone, make a separate page rn for choosing model and link em all, its a crotch right now" | **partial** — `home.html` lists tours, models, projects; `/` still opens the descent (PLAN 1.0, paused until he has organised it) |
| #46 | "build like separate "engine" application to create theese disassembly things"                         | **partial** — `editor.html` turns a GLB into a project; assemblies and tours are still hand-written JSON (PLAN 4.1)               |
| #31 | "we gotta build that thing from scratch with some tools that are gonna be usable to do so"             | **partial** — same as above                                                                                                       |
| #45 | "can u show me the engine-part of it in some form of report?"                                          | **done** — `docs/ENGINE.md`, `docs/ARCHITECTURE.md`                                                                               |

### Ease of use

| #   | Want                                                                                                             | Status                                                                                          |
| --- | ---------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| #2  | "we should add the dropdown-menu like design for vertical screens(phones) cuz just easier to view and see stuff" | **done** for the viewer — under 600 px the model name becomes a dropdown; editor not (PLAN 4.3) |

### Performance

| #   | Want                                                                                                            | Status                                                                                  |
| --- | --------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| #2  | "lets first figure out whats in his website that makes it work quick and generally show 3d model on js quickly" | **partial** — BVH picking, instancing, size budget; no measured fps pass yet (PLAN 2.5) |
| #2  | "maybe engine itself should something like his, but versatile thats gonna work on machine like mine"            | **partial** — same                                                                      |

### Bugs he reported

| #   | Report                                                                                                                                                                           | Status                                                                        |
| --- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| #50 | "just had a "dive in" button when chose bluetooth module - it dived into this, so works not correctly rn" (screenshot: a generic "silicon die" interior under the FC breadcrumb) | **resolved** — generated chip interiors removed; only author-made dives exist |
| #51 | "and this after choosing MCU something on that prev level so idk" (screenshot: the mould printed "Mould compound upper half")                                                    | **done** — `d07aa82`, marking reads STM32F405 RGT6                            |
| #42 | "wth xd" (screenshot)                                                                                                                                                            | unclear                                                                       |

## Decisions and what he turned down

- **Repository.** First "1 c so he see it yea" (#2, a branch on Oleg's repo); then "can we do it
  like separate project but link his stuff there or something or how you think would be better"
  (#5). Now: own repo `origin`, Oleg's as `upstream`, work on `engine`.
- **Engine shape.** "2 c" (#2): both a sandbox and saved descents (tours), sandbox first.
- **Descent content.** "whatever you think, im not sure whether that one hes using is good"
  (#2). Kept as the design reference (`index.html`).
- **Russian.** "id never publish ru version sry" (#2). Removed in `da6c498`.
- **Collaboration.** "i wanna cowork but i work mine and see what he do and viceversa" (#11);
  later: "Олег (OlehHavrilko) — коллаборатор, работает в своей ветке и через pull request"
  (#52: Oleg is a collaborator, works in his own branch, comes in through pull requests).
- **Credit and untouched files.** "подпись ровно "Created by f3nt and Oleh Havrilko", без
  ролей; ... package.json и LICENSE не трогали намеренно" (#52: credit exactly that, no roles;
  package.json and LICENSE deliberately unchanged).
- **Devices are data.** "устройства — только данные, без кода под конкретное устройство" (#52:
  devices are data only, no device-specific code).
- **Order of work.** Looks first, data animations "later when just view is good" (#29); stages
  "1) догнать демо по виду и дизайну (сейчас), 2) довести движок, 3) механические анимации по
  триггерам, 4) GUI" (#52: catch up with the demo's look now, then the engine, then
  trigger-driven mechanical animations, then GUI).
- **Arrows mean the descent's level stepping, not zoom.** "NOPOOOO zoom in i meant see how his
  shit is lkike" (#34), correcting the earlier "longest way of zoom-ins" (#30).

## How he wants the agent to work

- "dont change stuff u understand ... examine briefly ask questions bout what i want" (#1).
- "do maximum amount of tests but do it in small testable steps eventually so you dont rush"
  (#2).
- "split your job between any any amount of models and AI's subchats in parraalell as you
  want" (#2); "work on full effort full power mode include subagents try and test research
  things, but always kep up with a plan, tell me if anything going sideways" (#49).
- "if you wanna ask anything - only then interrupt your work" (#49); "tell me what else to do if
  only you need anything, everything else do urself(i grant you that, so teaching rule not
  apply here in this work)" (#40).
- Commits: "very short form, make it dumb and general as much as possible and compact(1
  paragraph no context or settings or stuff jus general idea)" (#3).
- Repository files: "none of context goes into files in this repo, just basic explanation of
  what code\idea does" (#5, confirmed #14). This file is the exception he asked for.
- Live preview: "i want testing with you live as you do it" (#31); "keep uploading progress to
  live demo" (#34); "check ytourself if it fits untill youre done" (#34).
- Facts: "dont imagine stuff or assume how it works, all the time compare results test yourself
  did you really solve a real problem" (#41); "Пиши только то, что реально так в коде или что мы
  решили в этом чате; где не уверен — так и пометь" (#52: write only what is really in the code
  or was decided; mark what is uncertain).
- Continuity: "я буду работать с телефона, с ноутбука и с другими агентами (Claude Code, Claude
  app, Grok), и любой из них должен продолжить с того же места" (#52: phone, laptop and other
  agents must be able to continue from the same point); "Дальше в конце каждой сессии: обнови
  docs/STATE.md, коммит, пуш" (#52: at the end of every session update STATE.md, commit, push).
- "Без force-push." (#52)
- "answer a short brief about what i asked so far and what you done" (#30).

## Open questions and ideas for later

- **Local AI** — "maybe include local ai if needed for quick work or stuff idk that we gonna
  figure out later" (#2).
- **Other agents** — Grok: "make that separate for now so dont touch ill figure out most of his
  out put myself, or makeng him a slave and just research basic stuff and taking output is
  enough?" (#6), "can u just connect him yourself?" (#7). DeepSeek: "i also have fuckin deepseek
  account we can merge it too somehow?" (#9). Undecided.
- **Pre-computed transitions** — #20 (quoted above).
- **Reference photos for textures** — he offered "ask me to get like cropped pictures" (#40);
  never taken up.
- **OBJ / 3MF** — "why no both?" (#2) for formats; only STL and GLB/glTF load.
- **Text readability and design notes** — "imma tell the design aspects when we test stuff"
  (#2).
- Unclear complaints, kept as quotes: "i see that in cut dont like that" (#45), "darker pcb u
  meant black xd?" (#35), "wth xd" (#42).
