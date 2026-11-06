import { useState } from 'react';

export const TOUR_STORAGE_KEY = 'fo4ide.editorsTour';

export const TOUR_STEPS: readonly string[] = [
  'Open Quest, Dialogue or Scene to start an editor.',
  'Hover over a ? or press Tab to it to read what a field means.',
  'While an editor screen has focus, Ctrl+Z undoes an edit and Ctrl+Y redoes it. Nothing is written to disk until you save the plugin.',
];

export function useTour() {
  const [done, setDone] = useState(() => localStorage.getItem(TOUR_STORAGE_KEY) === 'done');
  const [step, setStep] = useState(0);
  const skip = () => {
    localStorage.setItem(TOUR_STORAGE_KEY, 'done');
    setDone(true);
  };
  const next = () => {
    if (step + 1 >= TOUR_STEPS.length) skip();
    else setStep(step + 1);
  };
  return { active: !done, text: TOUR_STEPS[step], last: step + 1 >= TOUR_STEPS.length, next, skip };
}
