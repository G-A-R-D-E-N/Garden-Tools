// Constraint: a claim counts as an invariant only when it is listed in ENFORCED; the rest are measured statistics that never fail a tome.
export class Claims {
  constructor(enforced) {
    this.enforced = new Set(enforced);
    this.counts = new Map();
    this.examples = new Map();
  }
  check(name, ok, where) {
    let c = this.counts.get(name);
    if (!c) { c = { pass: 0, total: 0 }; this.counts.set(name, c); }
    c.total++;
    if (ok) { c.pass++; return true; }
    const list = this.examples.get(name) ?? [];
    if (list.length < 3) { list.push(where ?? ''); this.examples.set(name, list); }
    return false;
  }
  merge(other) {
    for (const [name, c] of other.counts) {
      const mine = this.counts.get(name) ?? { pass: 0, total: 0 };
      mine.pass += c.pass; mine.total += c.total;
      this.counts.set(name, mine);
      const list = this.examples.get(name) ?? [];
      for (const e of other.examples.get(name) ?? []) if (list.length < 3) list.push(e);
      if (list.length) this.examples.set(name, list);
    }
  }
  violations() {
    const out = [];
    for (const [name, c] of this.counts) {
      if (this.enforced.has(name) && c.pass !== c.total) out.push({ claim: name, failed: c.total - c.pass, of: c.total, where: this.examples.get(name) });
    }
    return out;
  }
  lines() {
    return [...this.counts.entries()].map(([name, c]) => `${this.enforced.has(name) ? 'invariant' : 'measured '} ${name}: ${c.pass}/${c.total}`);
  }
}
