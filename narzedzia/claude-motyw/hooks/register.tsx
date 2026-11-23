import type { Register, ThemeMessageRule, ThemeSetArgs } from 'claude-code'

import { colors, problems } from './palette'
import { SCENE } from './scene'

// K1 windows: radius 12, hairline line, soft shadow 3 px down (docs/WYGLAD.md "Okna", "Ikona").
const card: ThemeMessageRule = {
  radius: 12,
  borderColor: 'border',
  borderWidth: 1,
  padding: 14,
  shadow: { y: 3, blur: 8, color: '#0000001a' },
}

// The look without the conversation style, for builds that do not take `style`.
const BASE: ThemeSetArgs = {
  colors,
  backdrop: { scene: 'glina', glass: { blur: 20, opacity: 0.7 } },
}

const LOOK: ThemeSetArgs = {
  ...BASE,
  style: {
    // IBM Plex Sans for text, Plex Serif for headings: hierarchy by size and color, not weight.
    Text: { fontFamily: ['sans'], fontSize: 15, lineHeight: 23 },
    Heading: { fontFamily: ['serif'], fontWeight: 'regular' },
    UserMessage: { ...card, backgroundColor: 'bg-user-message', color: 'text-user-message' },
    AssistantMessage: { ...card, backgroundColor: 'bg-assistant-message', color: 'text-assistant-message' },
    ToolUse: { backgroundColor: 'surface-1', borderColor: 'border', borderWidth: 1, radius: 8, paddingX: 10, paddingY: 4 },
  },
}

export const register: Register = on => {
  on('session.start', async ($, e, next) => {
    const unpainted = problems.find(line => line.startsWith("Desktop won't paint"))

    if (unpainted !== undefined) {
      $.ui.toast(unpainted)
    }

    let set
    try {
      set = await $.theme.set(LOOK)
    } catch (error) {
      $.ui.log(String(error))
      set = await $.theme.set(BASE)
    }

    if (!set.isDrawn) {
      $.ui.status(set.reason)
    }

    return next(e)
  })

  on('ui.render', { component: 'Backdrop' }, ($, e, next) => {
    if (e.surface !== 'desktop' && e.surface !== 'mobile') {
      return next(e)
    }

    const { Svg } = $.ui.resolve(e)

    return <Svg source={SCENE} alt="Warstwy papieru w terakocie, jak tapeta Glina w Kons K1" isInteractive />
  })
}
