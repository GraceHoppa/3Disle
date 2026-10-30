# Decisions

The owner's answers from the review Q&A, one part at a time. Newest part at the bottom. Quotes
are verbatim. Where an answer here differs from `VISION.md`, this file wins.

Already decided before the review (not discussed again):

- The engine and editor come first, not specific devices. A user with no code loads a 3D model
  from Blender, selects parts (by object and by polygon), names them, and gets disassembly,
  section cut, X-Ray and dives into more detailed models.
- The order of work: match Oleg's demo in looks, then make the engine reliable, then mechanical
  animations on triggers, then a full GUI.
- Credit: "Created by f3nt and Oleh Havrilko", no roles. Oleg comes in through pull requests.
- iPhone 5 and RadioMaster were removed at the owner's request.

## 1. The idea and who it is for — 2026-10-08

**The drone and other devices.** Not postponed. They are just not the main goal; the engine is.

> "no i meant that its not a main goal, main goal is the engine, then that the end-result is
> fucking app with GUI that you can make your own html webpage like theese that we generated, but
> with bare custom 3dmodels and some scriptiong/tools/whatever to make it
> disassemble/zoomin/view electronics/logic/fucking props pushing air etc), designed as Oleg's
> reference page(by looks/mechanics/design)"

**Q: Is the VISION line ("the descent is the look to match; the product is an engine that
produces descents like it") still right?** Yes. The first goal is a base engine that can
produce at least what Oleg built by hand; the looks get better after that.

> "YEP, no i meant exactly that, looks will eventually be enhanced further more, what we need is
> base engine that gets atleast what oleg generated manually"

**Q: Who is the user?** Anyone who wants to make a teardown page with disassembly, zoom-in
and mechanics. The result opens in a separate viewer (a browser opening the HTML) or as a
preview inside the engine; either is fine.

> "Anyone who want to do this disassembly-zoom-in-view-teardown webpage\animation\some file with
> all that mechanics and stuff opened with a viewer application separately(just browser opener
> of html i suppose idk or just open it as preview inside engine itself dont matter)"

**Q: What does the finished output look like?** The flow: several 3D models of a device →
open the engine → load them → describe the logic → compile → a `.html` page with the scene as
the user set it up. The project itself keeps improving after that.

> "you have several 3d models of certain piece of tech/anything at all that consists of parts -
> you wanna do teardown - you open that engine, give it 3d models, say the logick anyhow ->
> compile\whatever->.html end resuilt(that page with scene programmed as the user programmed)"

**Q: Does it have to be fully local, or is hosted saving acceptable later?** Not decided yet:
the engine has to work first. He asked for a thorough review with many questions, one at a
time, to make sure the task and the environment are fully understood.

> "i want the engine to work first, so lets do a big review\q&a\dig me give ton of questions(that
> 1 by 1 interactive ones ill talk with you from there), make sure you understand the task
> completely and that your enviroment is known and understood"

## 2. The editor — 2026-10-08

**Q: What does "select parts by polygons" mean?** Not sure yet. Build both modes (click a
connected island; brush / lasso over faces) as a test, and he picks after trying them.

> "yes well test everythin"

**Q: Which formats?** GLB / glTF from Blender. He asked how well a good model's look carries
over to the page. Answer given (from the code, not yet tested on a real model): GLB keeps
geometry, names, PBR materials and textures (`src/models/load.ts` keeps them); Blender node
shaders and Cycles lighting do not carry over, so procedural materials must be baked to
textures; the page lights the model with its own lights and the demo's post chain. To verify:
load a free CC-licensed GPU GLB and compare.

> "GLB / glTF, blender but is it easily translated\converted to our webpage looks also how good
> can it copy the exact looks(if i have good 3d model of some gpu for example) how good will it
> look like in the webpage overview like content itself?"

**Q: How does a user describe the logic (dives, explode, spin, flows)?** Forms / panels for the
common things plus a small script box for the rest, to be tested.

> "so like can we test, i believe 4 gonna work fine"

**Q: Where does the editor run?** In the end, a desktop app that generates the finished HTML
pages. How scripting is presented inside it (popup window, terminal, anything) does not
matter to him.

> "desktop app, it should only generate html pages and stuff, but feel free to argue"

> "eventually the fucking engine that im gonna use to create those ready html pages is a
> desktop app, i dont care how scripting works from there as a popup window inside app itself a
> fuckin tmux or whatever else, just keep up with the plan"

**Q: A test model?** Find a free one (CC-licensed; report the license before using it).

## 3. The viewer — 2026-10-08

**Q: Is the exported page the same as the viewer?** No. The current viewer is a debugger and
will go eventually. The target: describe a build (e.g. a drone) → get or draw 3D models of the
device and every component → wire it up, script it, give it logic → the engine compiles it →
one plain HTML page to link from a website, which opens the teardown tour or free exploration,
whichever the author chose.

> "so viever currently is just debugger/ well get rid of it eventually, how i see it is -> i have
> some drone build -> i wanna specify how its built in a cool wasy -> i get/draw 3d models of
> all components and drone itself -> i do some wiring scripting fuckin give it some logic ->
> engine cooking it up compiling -> i have just plain html as an end result, that i can put on
> my vlog website as a link, and for anyone it opens this 3d model disassembly
> tour\exploration depending on what i chose, just plain fuckin start of it like that"

He attached a screenshot of the RTX tour's landing card: part labels are drawn over the card,
stacked in a column across the title.

**Q: How does a visitor move through the page?** As in the current pages (arrows, orbit,
tours); it works, but it lacks the demo's polish. How to get the design right is still open.

> "like we already did in our pages, i dunno it works pretty good just lacks alot of good-looks
> that oleg used in his project(idunno how to make you design everything neetly and beautifully
> so well figure that out)"

**Q: Must the level transition be the demo's continuous zoom?** Preferably yes. Any part can be
selected, zoomed into (separated) and inspected, with its own parts inside, at any depth; the
author decides which path through that tree is the main course (the longest chain).

> "preferably yeah but okay so i had this idea that any part can be selected and "zoomed
> in"(separated) and inspected, and inside it there could also be some parts like that, like
> anywhere, but the engine user that programs his webpage decides whatever is like main
> course(or longest link in that tree yk?)"

**Q: Which features matter most now?**

- Flows / animation: yes, and programmable rather than hard-coded, like Tinkercad but for the
  looks.
- Disassemble: yes, but it must work properly / differently: on complex geometry (the drone) it
  looks broken.
- Cut: not needed for now. X-Ray: mostly useless; add a Thermal view later instead.
- Inspector: yes. Labels: yes, but fix them (details from him later).

> "Flows / animation, "1 but make it work properly\diffrent, cuz currently with complicated
> geometry(like on drone) it looks broken, cut i dont need for now xray is kinda useless anyway
> better add thermal(later), inspector ofc labels(fix labels tho, ill tell you later) 4 yes and
> make it not hard-coded but programmable - like in fucking Tinkercad but just fopr the looks yk""

## 4. Dives and tours — 2026-10-08

**Q: Keep the auto-generated "generic chip" interiors?** No auto-generation. Everything is up
to the author: he reads the datasheet and makes or draws the 3D model himself, so it shows real
data. For chips without a datasheet: a premade library the author can pick from (common chips,
or a labelled black box that looks like a circuit part with "some data flows here"). It looks
right but has no logic behind it.

> "ye fully programmable, no template or generic stuff to begin with, author must look datasheet
> and generate\draw the 3d object himself so it shows real data, if chip has no datasheet - just
> have some premade (like some fuckin like basic knowledge chips that are everywhere or generic
> like "some data flow here" to labeled black box that looks like some circuit part, just looks
> original but has no logic behind it"

**Q: What happens when a visitor selects a part?** Both: highlight + info, then isolate it
(lift it out, move the rest away). To be tested.

> "i think both is good idea, tho gotta test that"

**Q: Is a deeper level a separate model or a closer look inside the same one?** A separate
model: card → PCB is a small jump, PCB → GPU chip is a big one, and the new object has its own
layers. Since every level can have many layers, keep the cost down: preload the next levels so
the zoom-in is smooth, but don't compute everything at once. The agent picks the method.

> "obviously separate 3dmodel cuz graphix card->pcb is not big jump, but pcb->graphix chip is big
> jump, and that new object has to have its own layers and stuff, accounting that were already
> doing multiple layers for any part it could get too big so i think some transitions must
> happen(like pre-download all parts so transition zooming in is smooth but no calculation of
> everything at the same time) idk man do whats better"

**Q: Keep the tour shape (main path, text per level, landing and finale)?** Yes, and the
visitor can also leave the main path: dive into a single motor to take it apart, watch it spin,
see current flow in it; dive into the ESC → its MCU; and so on.

> "yea but i can dive into a separate motor for example to disassemble it\see how it spins\see
> how electricity flows in it etc, dive in esc_>mcu motor etc"

## 5. Links and flows — 2026-10-08

**Q: Keep role-based link presets?** Drop them. Add logic instead, like Tinkercad, so a page
shows the actual flow, not just a dissected model.

> "ye drop them, but add like logic, like in fuckin tinkercad so we can show some more than just
> disected 3dmodel but the actual flow"

**Q: What should a link be able to show?** All of these: data / signal pulses, power /
current, real wires in 3D, and air / heat / motion. Add them one at a time; test each very
carefully, try to break it and debug it before moving to the next.

> "ye but add all of it Gradually one by one, test throurly like very carefully every aspect try
> to break stuff and debug before movving to next\diffrent step"

**Q: What does the author program?** Blocks (trigger → action), values with simple rules, and
a script, so authors can go beyond the built-ins (servos, other logic, lighting effects). The
common obvious things come built in.

> "1 + 2 + 3 so he can do like some other stuff(like servos or diffrent logic for example or like
> some fuckin lighting activity etc) 4 for generic obvious stuff"

**Q: Can visitors control anything?** Yes: the author decides which controls (sliders, buttons)
the page has.

## 6. The look compared with Oleg's demo — 2026-10-08

**Q: What to catch up on first?** All four, in this order: materials and light → story polish →
sound → continuous zoom.

> "Story polish, Materials + light, Sound, Continuous zoom, 3->2->4->1"

**Q: How to get the design right?** Both: copy Oleg's values (CSS, timings, shader settings)
one to one and check them side by side, then his notes on screenshots.

**Q: Is the look fixed or themeable?** Oleg's look is the default; theming comes later.

**Q: The test model.** Prefer a model that can be broken down more thoroughly (more parts, more
steps); if none is found, he will download whatever is needed.

> "find some piece that you can break down more thourly so more steps, if no ill do whatever u
> want"

## 7. Ready models and data — 2026-10-08

**Q: The future of JSON assemblies next to GLB?** Both stay; he is not familiar with the
formats and leaves the details to the agent.

> "idunno man this json glb things im not familiar with, do whats best so both i think"

**Q: What are the three current devices (RTX 5070 Ti, Pixel 6, drone) now?** Left to the
agent. What matters is that the engine can generate pages like Oleg's and that all the
discussed features can be tested. The agent's pick: the RTX 5070 Ti tour is the reference
example, rebuilt toward Oleg's GPU → Atom page as data only and compared side by side; Pixel 6
and the drone stay as test data; the drone is polished later.

> "i want the end result engine to be able to generate webpages like what Oleg designed, i dont
> know"

> "yardstick? i dont know whatever u find easy to test all features we discussed"

**Q: First items for the premade parts library?** All four: IC packages, passives, connectors,
and a labelled black box.

**Q: Should the engine enforce the facts rule (sources, "approx.")?** No. The page author
decides whether to show such warnings.

> "i dont know didnt understand question, if its bout that were not sure whether that pcb is
> exactly that looks or anything - i dont care user can decide if he wants to warn"

## 8. Oleg's descent code in the repo — 2026-10-08

**Q: Where does the descent live from now on?** The engine moves to a new, fresh repository
named **3Disle**, with only the engine files and the few descent pieces it needs. This repo stays
as the old fork and backup. Oleg should be able to see it and take part if he wants.

> "move engine files and rename that repo to 3Disle or something, or yeah just make new one, but
> i want oleg to see and take part if he wants too"

Confirmed afterwards: "New repo, fresh"; name "3Disle".

**Q: The descent's own browser checks?** He did not know what they were. After the
explanation, the real question became whether the new repo keeps a copy of Oleg's page for
comparison: no. Compare against his live site
(https://olehhavrilko.github.io/3dgpuanimation/). So the descent's checks stay here.

**Q: Track Oleg's ongoing changes upstream?** Not for now: his page is enough reference. It is
one good-looking page; the project is a way to create such pages.

> "hes changing stuff adding new but its enough reference for now(he has good design but one gpu
> demo is not a project its just good lookin webpage, what i want is a way to create them)"

**Q: Will Oleg build engine features?** He should be a co-author (collaborator) on GitHub. We
also keep our own copy so that we can split off again if something breaks.

> "i think ye he should just be co-author on GH but keep our version too if he breaks something
> there so we can split/separate again"

## 9. Pages and the start screen — 2026-10-08

- **Export form:** both. The author picks a folder (`index.html` + models + code, for hosting)
  or a single `.html` that opens straight from disk.
- **His website, where the pages will be linked:** not built yet.
- **The app's start screen:** recent projects with previews, plus New project / Open.
- **compare.html:** drop it.

## 10. Tests and build — 2026-10-08

- **Seeing results:** he checks from his phone (remote-controlling this session). He needs a way
  to open the dev site from the phone: through his Termux SSH, or on the local network (the PC
  is on ethernet, the phone on Wi-Fi). Method still to settle.

  > "yep expect me to check results from phone now(i already remote controlled it) just make some
  > kindaf way to see the website from my termux ssh or just on local network(tho my pc is
  > plugged eth and phone on wifi but i think it sees well figure that out)"

- **Exported pages must run smoothly on:** his PC, an average laptop, and phones.
- **Step gate:** the agent runs its own tests, then reports that a step is done. He agrees or
  says what is wrong, as always.

  > "your tests then you call me say that something is done - a agree or say that its wrong, like
  > always"

- **CI for 3Disle:** lint, types, tests and build on every push and PR.

## 11. Documentation — 2026-10-08

- **VISION.md and DECISIONS.md:** no decision yet. He wants to see both after the changes and
  then say whether either is worth keeping.
- **Docs for 3Disle:** all four sets: for agents (CLAUDE.md, STATE, PLAN, DECISIONS), for coders
  (architecture, field reference), for authors, and for visitors. The README GitHub shows is a
  short overview of a couple of paragraphs: what it is and what it does. Download and install
  steps go in separate `.md` files. The manual for authors comes once something works well.

  > "main README(that oppens on github when you open repo) - short couple paragrapgh overview of
  > what it is and what it does, then some .md files with download+install process, when we have
  > something working well do manual page yes"

- **Commit messages:** not the old "very short and dumb" rule, and no hard-coded rules for the
  agent's work. Messages should explain themselves without bloating the context.

  > "dont hard-copde any rules for ur work, work smarter, and no, i want comments to be
  > self-explainatory but not overload the context and look toomuch yk"

## 12. Working with agents — 2026-10-08

- **3Disle on GitHub:** private at first, with Oleg invited.
- **How changes land:** no preference; a standard project repo with Oleg as a member.
- **Other AIs:** Grok for research. Oleg connected him to ElevenLabs (or something like it),
  which can be hooked in; he can connect other models later. The main work and all editing
  stay with the main agent; small, easy, repetitive tasks go to others so the main agent does
  not spend time on cheap work.

  > "grok for research, also oleg connected me to some elevenlabs or something so we can connect
  > that, also i can connect6 other models but imma need time for that, but main work and editing
  > process is only on you, give small\easy\drilling tasks to others so you dont spend time on
  > cheap work"

- **Interruptions:** only for questions and "step done, check it". When he says go on, the
  agent goes on without stopping; if he wants to stop, he will say so.

  > "yes but if i tell you to go on you just go on not stopping, if i wanna stop and say something
  > to you ill do it"

- Earlier, during the review: split the work between subagents so the main agent stays on the
  conversation about design and results.

  > "also i want to split the job so you dont bloat up with context, try to separate job between
  > diffrent subagents so its easier on you to focus on main goal - talking with me bout design
  > and results"

## Credit — 2026-10-30

**Q: Who is credited?** Only the owner: "Created by f3nt". Oleg left the project and never worked
on the engine; GPU → Atom was the idea, the engine is the owner's. His MIT copyright stays in
`LICENSE` and `docs/ARCHITECTURE.md` because modules from GPU → Atom are copied into `src/lib/`
unchanged (MIT requires the notice), and the start page keeps a link to GPU → Atom as the inspiration.
