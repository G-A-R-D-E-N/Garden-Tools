import Database from "better-sqlite3";
import { DB_PATH } from "./paths.js";

const IMAGE_BASE = 0x140000000;

let db: Database.Database | undefined;

export function getDb(): Database.Database {
  if (!db) {
    db = new Database(DB_PATH, { readonly: true, fileMustExist: true });
  }
  return db;
}

// Accepts a full VA or a bare RVA, with/without "0x", any case; returns the RVA either way.
export function parseAddressToRva(input: string): number | null {
  const cleaned = input.trim().replace(/^0x/i, "");
  const value = parseInt(cleaned, 16);
  if (Number.isNaN(value)) return null;
  return value >= IMAGE_BASE ? value - IMAGE_BASE : value;
}

export function formatRva(rva: number): string {
  return "0x" + rva.toString(16);
}

export function formatVa(rva: number): string {
  return "0x" + (rva + IMAGE_BASE).toString(16);
}
