import { makeColors } from './make-colors.js'
import type { ThemeColors } from 'claude-code'

// K1 window themes (docs/WYGLAD.md "Motywy okien"): cream paper by day, warm night by evening.
const made = makeColors({
  label: 'K1 Glina',
  roles: { accent: '#BC5A37', danger: '#A8322A', success: '#5F8A6A', warning: '#A8834A', pro: '#8B6AA8' },
  light: {
    gray: {
      anchors: {
        'surface-0': '#F5F3EE',
        'surface-1': '#FAF9F6',
        'surface-2': '#FFFFFF',
        'text-primary': '#1F1E1C',
        'text-secondary': '#5E5C57',
        'text-muted': '#6E6B65',
      },
    },
    git: { added: '#5F8A6A', removed: '#A8322A', modified: '#8A6A35', merged: '#8B6AA8', closed: '#7A2E1C', conflicting: '#BC5A37', draft: '#6E6B65' },
  },
  dark: {
    roles: { accent: '#D97757' },
    gray: {
      anchors: {
        'surface-0': '#211F1D',
        'surface-1': '#262523',
        'surface-2': '#292826',
        'surface-3': '#2A2927',
        'text-primary': '#EEEBE5',
        'text-secondary': '#ABA79F',
        'text-muted': '#9E9A92',
      },
    },
    git: { added: '#A3B592', removed: '#E8907E', modified: '#DDB993', merged: '#C3A0AE', closed: '#D6937A', conflicting: '#E9A88B', draft: '#ABA79F' },
  },
})

export const problems = made.problems

// Message colors from the K1 window roles: "pole / karta" for the person, "tlo okna" for Claude.
export const colors: ThemeColors = {
  ...made.theme,
  inputs: {
    light: {
      ...made.theme.inputs.light,
      'bg-user-message': '#FFFFFF',
      'text-user-message': '#1F1E1C',
      'bg-assistant-message': '#FAF9F6',
      'text-assistant-message': '#1F1E1C',
    },
    dark: {
      ...made.theme.inputs.dark,
      'bg-user-message': '#302F2C',
      'text-user-message': '#EEEBE5',
      'bg-assistant-message': '#262523',
      'text-assistant-message': '#EEEBE5',
    },
  },
}
