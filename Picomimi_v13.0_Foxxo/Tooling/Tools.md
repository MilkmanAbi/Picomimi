# Picomimi Tooling

Picomimi MicroOS did not start as a small project. Over time, the codebase grew into a **large, tightly-coupled sketch exceeding several thousand lines**, making manual development increasingly painful. Simple changes required scrolling through massive files, refactors were risky, and experimenting with new ideas often meant breaking unrelated parts of the system.

To keep development sane, Picomimi introduced a **dedicated tooling layer** — a set of command-line utilities designed to manage, restructure, fix, and verify the codebase automatically. These tools exist to make the project *human-scalable*, turning a monstrous monolithic sketch into something navigable, testable, and maintainable.

The Picomimi toolchain is not about overengineering — it is about **survival**.

---

## Why Tooling Was Necessary

Working directly on a single, multi-thousand-line sketch quickly becomes unmanageable:

* Navigating the code is slow and error-prone
* Refactoring large sections risks subtle breakages
* Experimental changes accumulate technical debt
* Manual consistency checks do not scale

The tooling layer solves these problems by **automating structure, normalization, and verification**. Instead of forcing the developer to manually keep everything in sync, Picomimi’s tools handle the boring, fragile, and repetitive work — allowing development to focus on design and behavior rather than file management.

---

## The Picomimi Toolchain

Each tool serves a specific purpose in the development workflow. Together, they form a complete lifecycle for managing a large MicroOS codebase.

---

### **MRRP.py**

**Monolithic Repartition & Refactor Program**

MRRP.py is responsible for **breaking the kernel apart**.

It takes a large, monolithic Picomimi sketch and splits it into clean, structured module files. This makes the codebase navigable, allows individual subsystems to be worked on independently, and removes the need to scroll through thousands of lines just to find a single function.

MRRP.py is the foundation of Picomimi’s modular architecture.

---

### **MIAU.py**

**Monolithic INO Aggregator Utility**

MIAU.py does the opposite of MRRP.py.

Once development is complete, MIAU.py **reassembles all modules into a single monolithic INO** suitable for compilation and flashing on the RP2040. This preserves the modular workflow for developers while satisfying the build and deployment constraints of the platform.

To the compiler, it is one sketch.
To the developer, it is many cleanly separated modules.

---

### **NYAA.py**

**Normalize Your Architecture Automatically**

NYAA.py exists to **fix what humans inevitably break**.

It consumes structured metadata (such as JSON configurations) and automatically normalizes the codebase: stitching files together, correcting inconsistencies, and applying architecture-wide fixes. This is especially useful after large refactors or experimental changes, where manual cleanup would be slow and error-prone.

NYAA.py keeps the project structurally sane.

---

### **MROW.py**

**Mend & Review Our Weirdness**

MROW.py is the **verification layer**.

It scans modules, checks structure and syntax, and ensures that everything conforms to Picomimi’s expectations before the code is assembled or flashed. MROW.py does not build or modify the system — it simply answers one critical question:

> *Is this codebase still sane?*

---

## Toolchain Workflow

A typical Picomimi development flow looks like this:

```
MRRP.py  →  NYAA.py  →  MIAU.py  →  MROW.py
```

* **MRRP.py** splits the code into modules
* **NYAA.py** normalizes and fixes architecture-wide issues
* **MIAU.py** reassembles everything for deployment
* **MROW.py** verifies that nothing is broken

This workflow allows Picomimi to scale without collapsing under its own complexity.

---

## Conclusion

Picomimi’s tooling layer exists because **large projects demand structure**. The tools are not optional extras — they are what make long-term development possible. Without them, the project would regress into an unmaintainable monolith.

With them, Picomimi remains modular, readable, and evolvable — even as the codebase continues to grow.

> Built with determination ฅ(•ㅅ•❀)ฅ
> Maintained with tools, hacky solutions, and a little love ˗ˋˏ ♡ ˎˊ˗
