# AI Disclosure

> **Project:** 1986 Bluetooth Apple Keyboard  
> **Author:** Anton Bloch  
> **Date:** April 2026

---

## Purpose

This document describes how AI tools were used during development of this project.

## How AI Was Used

### Documentation (README, setup guides)

The hardware initialization guide in `README.md` was written and validated by the author, with diagrams created by Claude. synthesized from Dover Motion's official product documentation (User Guide, Software Guide, Quick Start Guide). AI (GitHub Copilot) was used to verify technical accuracy and assist with formatting.

### Code & Software Architecture

Source code, system architecture, and feature design in `main.cpp/` was implemented by hand, with chunks being implemented by Claude as per Claude's debugging suggestions. Design decisions were made by the author based on domain knowledge and project requirements.

### Engineering Best Practices

AI was consulted to verify choices around project structure, configuration patterns, and C++ programming design conventions. These were treated as a reference.

### Debugging & Problem-Solving

AI was used as a diagnostic tool to work through blocking technical issues (environment configuration, network problems, driver communication) that would have otherwise delayed productivity.

AI was not used for the following:
- Generating entire source code or algorithms
- Writing research content or analysis
- Making design decisions without author review
- Producing any output that was accepted without verification

## Tools

- **GitHub Copilot** (Claude 4.6 Opus) (Claude 4.7 Opus) — conversational debugging, documentation review, best-practice verification

---