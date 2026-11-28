import { useState } from 'react';
import {
  LIGHTING_SETTINGS_STORAGE_KEY, parseStoredLightingSettings, serializeLightingSettings, type LightingSettings,
} from './cellLighting';

export function readStoredSettings(): LightingSettings {
  try {
    return parseStoredLightingSettings(localStorage.getItem(LIGHTING_SETTINGS_STORAGE_KEY));
  } catch {
    return parseStoredLightingSettings(null);
  }
}

function writeStoredSettings(settings: LightingSettings): void {
  try {
    localStorage.setItem(LIGHTING_SETTINGS_STORAGE_KEY, serializeLightingSettings(settings));
  } catch {
    return;
  }
}

export function useLightingSettings(): [LightingSettings, (next: LightingSettings) => void] {
  const [settings, setSettings] = useState<LightingSettings>(readStoredSettings);
  const update = (next: LightingSettings) => {
    setSettings(next);
    writeStoredSettings(next);
  };
  return [settings, update];
}
