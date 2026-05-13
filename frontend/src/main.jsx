import React, { useState } from "react";
import { createRoot } from "react-dom/client";
import { supabase } from "./supabaseClient";
import "./style.css";

function line(text, colour="black", options={}) {
  return { text, colour, bold: !!options.bold, double_width: !!options.double_width, double_height: !!options.double_height, underline: !!options.underline, align: options.align || "left" };
}

function App() {
  const [title,setTitle]=useState("STAR PRINT");
  const [blackText,setBlackText]=useState("Normal black text");
  const [redText,setRedText]=useState("This line is red");
  const [footerText,setFooterText]=useState("Back to black");
  const [submitting,setSubmitting]=useState(false);
  const [message,setMessage]=useState("");

  async function submitPrintJob(e){
    e.preventDefault(); setSubmitting(true); setMessage("");
    const payload={title,two_colour:true,lines:[line(blackText,"black"),line(redText,"red",{bold:true}),line(footerText,"black")],feed_lines:3,cut:true,partial_cut:true};
    const { error } = await supabase.from("print_jobs").insert({ title, payload });
    setSubmitting(false);
    setMessage(error ? `Failed to queue print job: ${error.message}` : "Print job queued.");
  }

  return <main className="page"><section className="card">
    <div className="header"><h1>Star Cloud Print</h1><p>Submit a print job to the central printer queue.</p></div>
    <form onSubmit={submitPrintJob} className="form">
      <label>Title<input value={title} onChange={e=>setTitle(e.target.value)} /></label>
      <label>Black line<input value={blackText} onChange={e=>setBlackText(e.target.value)} /></label>
      <label>Red line<input value={redText} onChange={e=>setRedText(e.target.value)} /></label>
      <label>Final black line<input value={footerText} onChange={e=>setFooterText(e.target.value)} /></label>
      <button type="submit" disabled={submitting}>{submitting ? "Queuing..." : "Print"}</button>
    </form>
    {message && <p className="message">{message}</p>}
    <div className="note">This page inserts a job into Supabase. The central worker prints it locally.</div>
  </section></main>;
}

createRoot(document.getElementById("root")).render(<App />);
