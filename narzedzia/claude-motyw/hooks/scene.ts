// The K1 wallpaper "Glina" (docs/WYGLAD.md "Tapety"): long tilted paper sheets whose edges come
// down from the top and bend out at the bottom-right corner, a warm lit wall on each edge and a soft
// shadow on the sheet below. Matte edges, no glow. Clouds drift very slowly in the sky reflection.
const W = 1600
const H = 1000

// Back to front: top x of the edge, x where it leaves the bottom.
const SHEETS: readonly [number, number][] = [
  [1010, 1330],
  [1150, 1420],
  [1285, 1500],
  [1405, 1572],
]

function edge(top: number, bottom: number): string {
  return `M${top} 0C${top - 30} ${H * 0.42} ${top + 40} ${H * 0.8} ${bottom} ${H}`
}

function sheet(top: number, bottom: number, i: number): string {
  const d = `${edge(top, bottom)}L${W} ${H}L${W} 0Z`
  return [
    `<path class="shadow" filter="url(#soft)" transform="translate(-16 6)" d="${d}"/>`,
    `<path fill="url(#g${i})" d="${d}"/>`,
    `<path class="wall w${i}" d="${edge(top, bottom)}"/>`,
  ].join('')
}

const gradients = SHEETS.map(
  (_, i) =>
    `<linearGradient id="g${i}" x1="0" y1="0" x2="0.35" y2="1">` +
    `<stop offset="0" class="sky${i}"/><stop offset="1" class="s${i}"/></linearGradient>`,
).join('')

const CSS = `
svg { --bg0: #1a1512; --bg1: #2a1d17; --cloud: #e9a88b;
  --s0: #5c3324; --s1: #8f4a31; --s2: #b55c3e; --s3: #d97757;
  --k0: #74412e; --k1: #a65a3d; --k2: #c9714f; --k3: #e9a88b;
  --w0: #8a5039; --w1: #c06a49; --w2: #e08d6c; --w3: #f1c2aa; --sh: #0d0806; }
@media (prefers-color-scheme: light) {
  svg { --bg0: #f3efe8; --bg1: #e6ddd0; --cloud: #ffffff;
    --s0: #ead6c8; --s1: #e9b89f; --s2: #d97757; --s3: #bc5a37;
    --k0: #f4e7dc; --k1: #f0cdb9; --k2: #e59474; --k3: #cf6c49;
    --w0: #f7eee6; --w1: #f6dccd; --w2: #efb397; --w3: #e9a88b; --sh: #6b4a3a; }
}
.bg0 { stop-color: var(--bg0); } .bg1 { stop-color: var(--bg1); }
.s0 { stop-color: var(--s0); } .s1 { stop-color: var(--s1); } .s2 { stop-color: var(--s2); } .s3 { stop-color: var(--s3); }
.sky0 { stop-color: var(--k0); } .sky1 { stop-color: var(--k1); } .sky2 { stop-color: var(--k2); } .sky3 { stop-color: var(--k3); }
.wall { fill: none; stroke-width: 7; stroke-linecap: round; }
.w0 { stroke: var(--w0); } .w1 { stroke: var(--w1); } .w2 { stroke: var(--w2); } .w3 { stroke: var(--w3); }
.shadow { fill: var(--sh); opacity: 0.45; }
.cloud { fill: var(--cloud); opacity: 0.07; }
.drift { animation: drift 240s ease-in-out infinite alternate; }
.drift2 { animation: drift 320s ease-in-out infinite alternate-reverse; }
@keyframes drift { from { transform: translateX(-60px); } to { transform: translateX(80px); } }
`

export const SCENE = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" preserveAspectRatio="xMidYMid slice">
<style>${CSS}</style>
<defs>
<linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" class="bg0"/><stop offset="1" class="bg1"/></linearGradient>
${gradients}
<filter id="soft" x="-10%" y="-10%" width="120%" height="120%"><feGaussianBlur stdDeviation="14"/></filter>
<filter id="haze" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="40"/></filter>
</defs>
<rect width="${W}" height="${H}" fill="url(#bg)"/>
<g filter="url(#haze)">
<g class="drift"><ellipse class="cloud" cx="420" cy="180" rx="320" ry="70"/><ellipse class="cloud" cx="760" cy="120" rx="220" ry="50"/></g>
<g class="drift2"><ellipse class="cloud" cx="260" cy="760" rx="280" ry="60"/><ellipse class="cloud" cx="900" cy="620" rx="200" ry="44"/></g>
</g>
${SHEETS.map(([t, b], i) => sheet(t, b, i)).join('\n')}
</svg>`
