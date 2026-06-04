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

function pushText(bytes, text) {
  bytes.push(...asciiBytes(text));
}

function pushLine(bytes, text = "") {
  pushText(bytes, text);
  bytes.push(LF);
}

function align(bytes, mode) {
  const n = mode === "center" || mode === "centre" ? 1 : mode === "right" ? 2 : 0;
  bytes.push(ESC, GS, 0x61, n);
}

function bold(bytes, enabled) {
  bytes.push(ESC, enabled ? 0x45 : 0x46);
}

function doubleWidth(bytes, enabled) {
  bytes.push(ESC, 0x57, enabled ? 1 : 0);
}

function black(bytes) {
  bytes.push(ESC, 0x35);
}

function wrapLine(line, width = 32) {
  const words = normaliseForStar(line).split(/\s+/);
  const out = [];
  let current = "";

  for (const word of words) {
    if (!word) continue;

    if (!current) {
      current = word;
    } else if ((current + " " + word).length <= width) {
      current += " " + word;
    } else {
      out.push(current);
      current = word;
    }
  }

  if (current) out.push(current);
  return out.length ? out : [""];
}

function messageLines(body) {
  return normaliseForStar(body)
    .replace(/\r\n/g, "\n")
    .split("\n")
    .flatMap((paragraph) => wrapLine(paragraph.trimEnd(), 32))
    .filter((line, index, arr) => {
      if (line !== "") return true;
      return index > 0 && index < arr.length - 1;
    });
}

function bytesToBase64(bytes) {
  let binary = "";
  const chunkSize = 0x8000;

  for (let i = 0; i < bytes.length; i += chunkSize) {
    const chunk = bytes.slice(i, i + chunkSize);
    binary += String.fromCharCode(...chunk);
  }

  return btoa(binary);
}

function buildStarBytes(name, body) {
  const bytes = [];
  const safeName = normaliseForStar(name.trim() || "Anonymous");

  bytes.push(ESC, 0x40);
  bytes.push(ESC, RS, 0x43, 1);

  align(bytes, "center");
  bytes.push(ESC, FS, 0x70, 1, 0);
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

  for (const line of messageLines(body)) {
    pushLine(bytes, line);
  }

  black(bytes);
  align(bytes, "left");
  bytes.push(ESC, 0x61, 3);
  bytes.push(ESC, 0x64, 3);

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
      setMessage("Please enter a message for the printer.");
      return;
    }

    const { error } = await supabase
      .from("print_jobs")
      .insert({
        title: payload.title,
        payload,
      });

    setSubmitting(false);

    if (error) {
      setMessage(`Failed to queue print job: ${error.message}`);
      return;
    }

    setName("");
    setBody("");
    setMessage("Message queued. Stand by for ridiculous machinery.");
  }

  return (
    <main className="thing-page">
      <div className="stripe stripe-red"></div>
      <div className="stripe stripe-yellow"></div>
      <div className="stripe stripe-purple"></div>

      <section className="hero">
        <div className="tv-panel" aria-hidden="true">
          <div className="tv-screen">
            <div className="colour-bars"></div>
            <div className="screen-copy">
              <strong className="mr">MR.</strong>
              <strong className="thing">THING</strong>
            </div>
          </div>
          <div className="tv-side">
            <span></span><span></span><span></span>
          </div>
        </div>

        <section className="card">
          <div className="kicker">THE CULT COMEDY TV SHOW SHOW</div>
          <h1>Print a Message</h1>
          <p className="intro">Type your name, write your message, and fire it into the Thing printer.</p>

          <form onSubmit={submitPrintJob} className="form">
            <label>
              <span>Name</span>
              <input
                value={name}
                onChange={(event) => setName(event.target.value)}
                placeholder="Your name"
                maxLength={64}
              />
            </label>

            <label>
              <span>Message</span>
              <textarea
                value={body}
                onChange={(event) => setBody(event.target.value)}
                placeholder="Write your message here..."
                rows={8}
                maxLength={1500}
              />
            </label>

            <button type="submit" disabled={submitting}>
              {submitting ? "Broadcasting..." : "Print message"}
            </button>
          </form>

          {message && <p className="message">{message}</p>}
        </section>
      </section>
    </main>
  );
}

createRoot(document.getElementById("root")).render(<App />);
