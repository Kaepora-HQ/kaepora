/** A file's SHA-256 as hex, as the tables and a run's record name a file. */
export const sha256 = (bytes: Uint8Array) =>
	new Bun.CryptoHasher("sha256").update(bytes).digest("hex");
