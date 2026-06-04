import React, { useMemo, useState } from "react";
import { createRoot } from "react-dom/client";
import { supabase } from "./supabaseClient";
import "./style.css";

const ESC = 0x1b;
const FS = 0x1c;
const GS = 0x1d;
const RS = 0x1e;
const LF = 0x0a;

function normaliseForStar(text) {
  return String(text)
    .replace(/[‘’‚‛]/g, "'")
    .replace(/[“”„]/g, '"')
    .replace(/[–—−]/g, "-")
    .replace(/…/g, "...")
    .replace(/\u00a0/g, " ");
}

function asciiBytes(text) {
  const clean = normaliseForStar(text);
  const bytes = [];
  for (let i = 0; i < clean.length; i++) {
    const code = clean.charCodeAt(i);
    if (code === 0x0a || code === 0x0d || code === 0x09 || (code >= 0x20 && code <= 0x7e)) {
      bytes.push(code);
    } else {
      bytes.push("?".charCodeAt(0));
    }
  }
  return bytes;
}

function pushText(bytes, text) { bytes.push(...asciiBytes(text)); }
function pushLine(bytes, text = "") { pushText(bytes, text); bytes.push(LF); }

function align(bytes, mode) {
  const n = mode === "center" || mode === "centre" ? 1 : mode === "right" ? 2 : 0;
  bytes.push(ESC, GS, 0x61, n);
}
function bold(bytes, enabled) { bytes.push(ESC, enabled ? 0x45 : 0x46); }
function doubleWidth(bytes, enabled) { bytes.push(ESC, 0x57, enabled ? 1 : 0); }
function black(bytes) { bytes.push(ESC, 0x35); }

function wrapLine(line, width = 32) {
  const words = normaliseForStar(line).split(/\s+/);
  const out = [];
  let current = "";
  for (const word of words) {
    if (!word) continue;
    if (!current) current = word;
    else if ((current + " " + word).length <= width) current += " " + word;
    else { out.push(current); current = word; }
  }
  if (current) out.push(current);
  return out.length ? out : [""];
}

function messageLines(body) {
  return normaliseForStar(body)
    .replace(/\r\n/g, "\n")
    .split("\n")
    .flatMap((paragraph) => wrapLine(paragraph.trimEnd(), 32))
    .filter((line, index, arr) => line !== "" || (index > 0 && index < arr.length - 1));
}

function bytesToBase64(bytes) {
  let binary = "";
  const chunkSize = 0x8000;
  for (let i = 0; i < bytes.length; i += chunkSize) {
    binary += String.fromCharCode(...bytes.slice(i, i + chunkSize));
  }
  return btoa(binary);
}

function buildStarBytes(name, body) {
  const bytes = [];
  const safeName = normaliseForStar(name.trim() || "Anonymous");

  bytes.push(ESC, 0x40);             // initialise
  bytes.push(ESC, RS, 0x43, 1);      // two-colour mode

  align(bytes, "center");
  bytes.push(ESC, FS, 0x70, 1, 0);   // print saved logo 1, normal size
  bytes.push(LF);

  black(bytes);
  bold(bytes, true);
  doubleWidth(bytes, true);
  pushLine(bytes, "NEW MESSAGE");
  doubleWidth(bytes, false);
  bold(bytes, false);
  bytes.push(LF);

  align(bytes, "left");
  black(bytes);
  bold(bytes, true);
  pushLine(bytes, `From: ${safeName}`);
  bold(bytes, false);
  pushLine(bytes, "--------------------------------");

  for (const line of messageLines(body)) pushLine(bytes, line);

  black(bytes);
  align(bytes, "left");
  bytes.push(ESC, 0x61, 3);          // feed 3 lines
  bytes.push(ESC, 0x64, 3);          // feed to cut, partial cut

  return bytes;
}

function App() {
  const [name, setName] = useState("");
  const [body, setBody] = useState("");
  const [submitting, setSubmitting] = useState(false);
  const [message, setMessage] = useState("");

  const payload = useMemo(() => {
    const safeName = name.trim() || "Anonymous";
    const starBytes = buildStarBytes(name, body);
    return {
      title: `Message from ${safeName}`,
      name: safeName,
      body,
      star_b64: bytesToBase64(starBytes),
      print_logo: true,
      logo_number: 1,
      logo_mode: 0,
      two_colour: true,
      feed_lines: 3,
      cut: true,
      partial_cut: true,
    };
  }, [name, body]);

  async function submitPrintJob(event) {
    event.preventDefault();
    setSubmitting(true);
    setMessage("");
    if (!body.trim()) {
      setSubmitting(false);
      setMessage("Please enter a message.");
      return;
    }
    const { error } = await supabase.from("print_jobs").insert({ title: payload.title, payload });
    setSubmitting(false);
    if (error) {
      setMessage(`Failed to queue print job: ${error.message}`);
      return;
    }
    setName("");
    setBody("");
    setMessage("Message queued for printing.");
  }

  return (
    <main className="page">
      <section className="card">
        <div className="header">
          <h1>Print a Message</h1>
          <p>Send a message to the central Star printer.</p>
        </div>
        <form onSubmit={submitPrintJob} className="form">
          <label>Name
            <input value={name} onChange={(e) => setName(e.target.value)} placeholder="Your name" maxLength={64} />
          </label>
          <label>Message
            <textarea value={body} onChange={(e) => setBody(e.target.value)} placeholder="Write your message here..." rows={8} maxLength={1500} />
          </label>
          <button type="submit" disabled={submitting}>{submitting ? "Queuing..." : "Print message"}</button>
        </form>
        {message && <p className="message">{message}</p>}
        <details className="advanced">
          <summary>Preview queue payload</summary>
          <pre>{JSON.stringify({ ...payload, star_b64: `${payload.star_b64.slice(0, 80)}...` }, null, 2)}</pre>
        </details>
      </section>
    </main>
  );
}

createRoot(document.getElementById("root")).render(<App />);
