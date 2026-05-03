# LumiEirb

# LumiEirb – RISC-V Contest 2025–2026

This repository contains the work of Team **LumiEirb** for the **6th National RISC-V Student Contest**, focusing on the acceleration of FFT-512 on the CV32A6 processor using a CV-X-IF coprocessor.

---

## 📄 Report

The full scientific report describing the architecture, optimizations, and results is available here:

👉 https://github.com/fizera-gi/LumiEirb/tree/main/cva6-softcore-contest/report

---

## ⚙️ Project Structure

* `cva6-softcore-contest/`
  Main project directory based on the official contest repository.
  

* `cvxif_coprocessor_example/`
  Contains the implementation of the custom **CV-X-IF coprocessor**, including:

  * custom instruction decoding
  * coprocessor ALU (`copro_alu`)
  * integration with the CVA6 pipeline


* `report/`
  Contains the final report (PDF and sources).
  Includes execution logs required for submission:

  * GDB logs
  * UART outputs (picocom)
  * Benchmark results (FFT, CoreMark, MNIST)

---

## 🚀 Results Summary

The optimized design achieves:

* **41% cycle reduction** for FFT-512
  (98,951 vs. 167,569 cycles)

* **41.1% instruction count reduction**

* Successful execution of:

  * FFT (validated ✔)
  * CoreMark ✔
  * MNIST ✔

* FPGA implementation on **Zybo Z7-20**

---

## 🧠 Key Contributions

* Integration of a **CV-X-IF coprocessor**
* Design of custom instructions:

  * Complex arithmetic (CADD, CSUB, CMUL)
  * Fused butterfly (BFLY2)
* Refactoring of KISS FFT:

  * From recursive → iterative implementation
  * Specialized for N = 512

---

## 🔁 Reproducibility

The project can be reproduced using the official CVA6 contest framework:

👉 https://github.com/ThalesGroup/cva6-softcore-contest

All modifications are included in this repository.

---

## 📅 Submission

May 2026


