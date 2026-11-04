import './home.css';

/** The home page: every tour and model the engine knows, linked into the viewer. */
interface Listed {
  name: string;
  file: string;
  note?: string;
  hidden?: boolean;
}

async function list(index: string): Promise<Listed[]> {
  try {
    const res = await fetch(index);
    return res.ok ? ((await res.json()) as Listed[]).filter((m) => !m.hidden) : [];
  } catch {
    return [];
  }
}

function card(m: Listed, file: string) {
  const a = document.createElement('a');
  a.href = `viewer.html?model=${encodeURIComponent(file)}`;
  const name = Object.assign(document.createElement('strong'), { textContent: m.name });
  const note = Object.assign(document.createElement('small'), { textContent: m.note ?? '' });
  a.append(name, note);
  const li = document.createElement('li');
  li.append(a);
  return li;
}

const models = await list('models/index.json');
const projects = await list('projects/index.json');
const isTour = (m: Listed) => m.file.endsWith('tour.json');
document.getElementById('tours')!.append(...models.filter(isTour).map((m) => card(m, m.file)));
document
  .getElementById('models')!
  .append(
    ...models.filter((m) => !isTour(m)).map((m) => card(m, m.file)),
    ...projects.map((m) => card(m, `projects/${m.file}`)),
  );
