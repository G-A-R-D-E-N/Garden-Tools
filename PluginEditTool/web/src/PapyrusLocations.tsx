export interface LocationRow {
  key: string;
  title: string;
  detail: string;
  where: string;
  file: string | null;
  start: number;
  length: number;
}

export default function PapyrusLocations({ rows, empty, onPick }: {
  rows: LocationRow[];
  empty: string;
  onPick: (row: LocationRow) => void;
}) {
  if (rows.length === 0) return <div className="papyrus-log-empty">{empty}</div>;
  return (
    <div className="papyrus-locations">
      {rows.map(row => (
        <button key={row.key} className="papyrus-location-row" title={row.where} onClick={() => onPick(row)}>
          <span className="papyrus-location-title">{row.title}</span>
          <span className="papyrus-location-detail">{row.detail}</span>
          <span className="papyrus-location-where">{row.where}</span>
        </button>
      ))}
    </div>
  );
}
