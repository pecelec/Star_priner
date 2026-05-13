import React, { useMemo, useState } from "react";
import { createRoot } from "react-dom/client";
import { supabase } from "./supabaseClient";
import "./style.css";

function lineToJobLine(text, colour = "black", options = {}) {
  return {
    text,
    colour,
    bold: Boolean(options.bold),
    double_width: Boolean(options.double_width),
    double_height: Boolean(options.double_height),
    underline: Boolean(options.underline),
    align: options.align || "left",
  };
}

function normaliseMessageBody(body) {
  return body
    .replace(/\r\n/g, "\n")
    .split("\n")
    .map((line) => line.trimEnd())
    .filter((line) => line.length > 0);
}

function App() {
  const [name, setName] = useState("");
  const [body, setBody] = useState("");
  const [submitting, setSubmitting] = useState(false);
  const [message, setMessage] = useState("");

  const payload = useMemo(() => {
    const safeName = name.trim() || "Anonymous";
    const messageLines = normaliseMessageBody(body);

    return {
      title: "NEW MESSAGE",
      print_logo: true,
      logo_number: 1,
      logo_mode: 0,
      two_colour: true,
      lines: [
        lineToJobLine(`From: ${safeName}`, "black", { bold: true }),
        lineToJobLine("--------------------------------", "black"),
        ...messageLines.map((line) => lineToJobLine(line, "black")),
      ],
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

    const { error } = await supabase
      .from("print_jobs")
      .insert({
        title: `Message from ${name.trim() || "Anonymous"}`,
        payload,
      });

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
          <label>
            Name
            <input
              value={name}
              onChange={(event) => setName(event.target.value)}
              placeholder="Your name"
              maxLength={64}
            />
          </label>

          <label>
            Message
            <textarea
              value={body}
              onChange={(event) => setBody(event.target.value)}
              placeholder="Write your message here..."
              rows={8}
              maxLength={1500}
            />
          </label>

          <button type="submit" disabled={submitting}>
            {submitting ? "Queuing..." : "Print message"}
          </button>
        </form>

        {message && <p className="message">{message}</p>}

        <details className="advanced">
          <summary>Preview print payload</summary>
          <pre>{JSON.stringify(payload, null, 2)}</pre>
        </details>
      </section>
    </main>
  );
}

createRoot(document.getElementById("root")).render(<App />);
