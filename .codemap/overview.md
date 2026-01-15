# Code Map Overview

This document provides a high-level overview of the codebase architecture.

## Module Dependency Graph

### Core

- `include`
- `src`

## Modules

- [include](modules/include.md) – Provides core abstractions for building and executing dataflow graphs with resource management, node execution, and concurrency control.
- [src](modules/src.md) – The `src` module implements a task execution engine with support for asynchronous execution, work-stealing thread pools, and graph-based task scheduling.
---
*Generated: 2026-01-15T10:39:08 | llmap v0.1.0*
