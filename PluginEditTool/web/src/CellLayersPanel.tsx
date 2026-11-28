import { Eye, EyeOff } from 'lucide-react';
import CellCollapsible from './CellCollapsible';
import {
  EXPOSURE_MAX, EXPOSURE_MIN, LIGHT_GAIN_MAX, LIGHT_GAIN_MIN, TOGGLEABLE_LAYERS, clampExposure, clampLightGain,
  withLayerFlag, withLightingFlag, type LightingFlag, type LightingSettings, type ToggleableLayer,
} from './cellLighting';

const LAYER_LABELS: Record<ToggleableLayer, string> = {
  room: 'Room bounds',
  trigger: 'Triggers',
  marker: 'Markers',
  effect: 'Effects',
};

const LIGHTING_ROWS: { key: LightingFlag; label: string }[] = [
  { key: 'fog', label: 'Cell fog' },
  { key: 'lights', label: 'Cell lights' },
  { key: 'ambient', label: 'Cell ambient' },
  { key: 'markers', label: 'Light markers' },
];

export default function CellLayersPanel({ typeGroups, hiddenTypes, onToggleType, onShowAllTypes, settings, onSettingsChange }: {
  typeGroups: [string, number][];
  hiddenTypes: ReadonlySet<string>;
  onToggleType: (type: string) => void;
  onShowAllTypes: () => void;
  settings: LightingSettings;
  onSettingsChange: (next: LightingSettings) => void;
}) {
  return (
    <div className="cell-layers-pane">
      <CellCollapsible
        storageKey="cell.section.layers"
        title={`Layers (${typeGroups.length})`}
        actions={hiddenTypes.size > 0 ? <button className="papyrus-copy" onClick={onShowAllTypes}>Show all</button> : null}
      >
        {typeGroups.length === 0 ? (
          <div className="cell-suggest-empty">Load a cell to list its record types.</div>
        ) : (
          <div className="cell-layers-list">
            {typeGroups.map(([type, count]) => {
              const hidden = hiddenTypes.has(type);
              return (
                <div
                  key={type}
                  className={`cell-layer-row${hidden ? ' hidden' : ''}`}
                  onClick={() => onToggleType(type)}
                  title={hidden ? 'Show' : 'Hide'}
                >
                  {hidden ? <EyeOff size={13} /> : <Eye size={13} />}
                  <span className="cell-layer-name">{type}</span>
                  <span className="cell-layer-count">{count}</span>
                </div>
              );
            })}
          </div>
        )}
      </CellCollapsible>

      <CellCollapsible storageKey="cell.section.categories" title="Categories">
        <div className="cell-toggle-list">
          {TOGGLEABLE_LAYERS.map(key => (
            <label className="cell-toggle-row" key={key}>
              <input
                type="checkbox"
                checked={settings.layers[key]}
                onChange={e => onSettingsChange(withLayerFlag(settings, key, e.target.checked))}
              />
              {LAYER_LABELS[key]}
            </label>
          ))}
        </div>
      </CellCollapsible>

      <CellCollapsible storageKey="cell.section.lighting" title="Lighting">
        <div className="cell-toggle-list">
          {LIGHTING_ROWS.map(row => (
            <label className="cell-toggle-row" key={row.key}>
              <input
                type="checkbox"
                checked={settings[row.key]}
                onChange={e => onSettingsChange(withLightingFlag(settings, row.key, e.target.checked))}
              />
              {row.label}
            </label>
          ))}
          <label className="cell-range-row">
            <span>Light gain</span>
            <input
              type="range"
              min={LIGHT_GAIN_MIN}
              max={LIGHT_GAIN_MAX}
              step={0.05}
              value={settings.gain}
              onChange={e => onSettingsChange({ ...settings, gain: clampLightGain(Number(e.target.value)) })}
            />
            <output>{settings.gain.toFixed(2)}x</output>
          </label>
          <label className="cell-range-row">
            <span>Exposure</span>
            <input
              type="range"
              min={EXPOSURE_MIN}
              max={EXPOSURE_MAX}
              step={0.05}
              value={settings.exposure}
              onChange={e => onSettingsChange({ ...settings, exposure: clampExposure(Number(e.target.value)) })}
            />
            <output>{settings.exposure.toFixed(2)}</output>
          </label>
          <button
            type="button"
            className="papyrus-copy cell-neutral-btn"
            aria-pressed={settings.neutral}
            onClick={() => onSettingsChange(withLightingFlag(settings, 'neutral', !settings.neutral))}
          >
            Neutral lighting
          </button>
        </div>
      </CellCollapsible>
    </div>
  );
}
