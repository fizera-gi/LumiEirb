// Copyright 2024 Thales DIS France SAS
//
// Licensed under the Solderpad Hardware Licence, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
// You may obtain a copy of the License at https://solderpad.org/licenses/
//
// Original Author: Guillaume Chauvon

module copro_alu
  import cvxif_instr_pkg::*;
#(
    parameter int unsigned NrRgprPorts = 2,
    parameter int unsigned XLEN = 32,
    parameter type hartid_t = logic,
    parameter type id_t = logic,
    parameter type registers_t = logic

) (
    input  logic                  clk_i,
    input  logic                  rst_ni,
    input  registers_t            registers_i,
    input  opcode_t               opcode_i,
    input  hartid_t               hartid_i,
    input  id_t                   id_i,
    input  logic       [     4:0] rd_i,
    output logic       [XLEN-1:0] result_o,
    output hartid_t               hartid_o,
    output id_t                   id_o,
    output logic       [     4:0] rd_o,
    output logic                  valid_o,
    output logic                  we_o
);

  logic [XLEN-1:0] result_n, result_q;
  hartid_t hartid_n, hartid_q;
  id_t id_n, id_q;
  logic valid_n, valid_q;
  logic [4:0] rd_n, rd_q;
  logic we_n, we_q;


  logic signed [15:0] ar, ai, br, bi;
  logic signed [15:0] xr, xi, wr, wi;

  logic signed [31:0] p1, p2, p3, p4;
  logic signed [31:0] real_q30, imag_q30;
  logic signed [31:0] tr_q30, ti_q30;

  logic signed [15:0] real_q15, imag_q15;
  logic signed [15:0] tr, ti;
  
  
  // rs1=x0, rs2=x1, rs3=tw
  logic signed [15:0] x0r, x0i, x1r, x1i, twr, twi;
  logic signed [15:0] y0r, y0i, y1r, y1i;
  
  logic [XLEN-1:0] shadow_tw_n, shadow_tw_q;
  logic [XLEN-1:0] shadow_y1_n, shadow_y1_q;

  assign result_o = result_q;
  assign hartid_o = hartid_q;
  assign id_o     = id_q;
  assign valid_o  = valid_q;
  assign rd_o     = rd_q;
  assign we_o     = we_q;

  always_comb begin
   

    result_n = '0;
    hartid_n = hartid_i;
    id_n     = id_i;
    valid_n  = 1'b0;
    rd_n     = rd_i;
    we_n     = 1'b0;


    xr = '0; xi = '0; wr = '0; wi = '0;
    p1 = '0; p2 = '0; p3 = '0; p4 = '0;
    real_q30 = '0; imag_q30 = '0;
    real_q15 = '0; imag_q15 = '0;
    
    shadow_tw_n = shadow_tw_q;
    shadow_y1_n = shadow_y1_q;
    
    case (opcode_i)
      cvxif_instr_pkg::NOP: begin
        result_n = '0;
        hartid_n = hartid_i;
        id_n     = id_i;
        valid_n  = 1'b1;
        rd_n     = '0;
        we_n     = '0;
      end
      cvxif_instr_pkg::ADD: begin
        result_n = registers_i[1] + registers_i[0];
        hartid_n = hartid_i;
        id_n     = id_i;
        valid_n  = 1'b1;
        rd_n     = rd_i;
        we_n     = 1'b1;
      end
      cvxif_instr_pkg::DOUBLE_RS1: begin
        result_n = registers_i[0] + registers_i[0];
        hartid_n = hartid_i;
        id_n     = id_i;
        valid_n  = 1'b1;
        rd_n     = rd_i;
        we_n     = 1'b1;
      end
      cvxif_instr_pkg::DOUBLE_RS2: begin
        result_n = registers_i[1] + registers_i[1];
        hartid_n = hartid_i;
        id_n     = id_i;
        valid_n  = 1'b1;
        rd_n     = rd_i;
        we_n     = 1'b1;
      end
      
      cvxif_instr_pkg::SETTW_I16: begin
	  shadow_tw_n = registers_i[0];   
	  valid_n = 1'b1;
	  we_n    = 1'b0;                 // pas de writeback
	end
	
      cvxif_instr_pkg::MADD_RS3_R4: begin
        result_n = NrRgprPorts == 3 ? (registers_i[0] + registers_i[1] + registers_i[2]) : (registers_i[0] + registers_i[1]);
        hartid_n = hartid_i;
        id_n = id_i;
        valid_n = 1'b1;
        rd_n = rd_i;
        we_n = 1'b1;
      end
      cvxif_instr_pkg::MSUB_RS3_R4: begin
        result_n = NrRgprPorts == 3 ? (registers_i[0] - registers_i[1] - registers_i[2]) : (registers_i[0] - registers_i[1]);
        hartid_n = hartid_i;
        id_n = id_i;
        valid_n = 1'b1;
        rd_n = rd_i;
        we_n = 1'b1;
      end
      cvxif_instr_pkg::NMADD_RS3_R4: begin
        result_n = NrRgprPorts == 3 ? ~(registers_i[0] + registers_i[1] + registers_i[2]) : ~(registers_i[0] + registers_i[1]);
        hartid_n = hartid_i;
        id_n = id_i;
        valid_n = 1'b1;
        rd_n = rd_i;
        we_n = 1'b1;
      end
      cvxif_instr_pkg::NMSUB_RS3_R4: begin
        result_n = NrRgprPorts == 3 ? ~(registers_i[0] - registers_i[1] - registers_i[2]) : ~(registers_i[0] - registers_i[1]);
        hartid_n = hartid_i;
        id_n = id_i;
        valid_n = 1'b1;
        rd_n = rd_i;
        we_n = 1'b1;
      end
   
      // ----------------------------------------------------------------------
      // CMUL_I16
      //
      // Custom Q15 complex multiplication:
      //   R = A * B
      //   real = ar*br - ai*bi
      //   imag = ar*bi + ai*br
      //
      // The rounding step is intentionally identical to the software KISS FFT
      // macro so that the optimized implementation remains bit-exact.
      // ----------------------------------------------------------------------
      
      cvxif_instr_pkg::CMUL_I16: begin
	  // rs1
	  ar = registers_i[1][15:0];
	  ai = registers_i[1][31:16];

	  // rs2
	  br = registers_i[0][15:0];
	  bi = registers_i[0][31:16];

	  // Q30 multiply
	  real_q30 = ar * br - ai * bi;
	  imag_q30 = ar * bi + ai * br;

	  // Q15 with rounding
	  real_q15 = (real_q30 + (1 <<< 14)) >>> 15;
	  imag_q15 = (imag_q30 + (1 <<< 14)) >>> 15;

	  result_n = {imag_q15, real_q15};
	  valid_n  = 1'b1;
	  rd_n     = rd_i;
	  we_n     = 1'b1;
	end
	
	
      //----------------------------------------------------------------------
      // CADD_I16
      //
      // Packed Q15 complex addition:
      //   result.real = rs1.real + rs2.real
      //   result.imag = rs1.imag + rs2.imag
      //
      // This instruction replaces the KISS FFT C_ADD/C_ADDTO macros.
      // ----------------------------------------------------------------------
      
	      cvxif_instr_pkg::CADD_I16: begin
	  // Unpack: {imag, real}
	  xr = registers_i[0][15:0];     // real
	  xi = registers_i[0][31:16];    // imag
	  wr = registers_i[1][15:0];     // real
	  wi = registers_i[1][31:16];    // imag

	  real_q15 = xr + wr;
	  imag_q15 = xi + wi;

	  result_n = {imag_q15, real_q15};

	  hartid_n = hartid_i;
	  id_n     = id_i;
	  valid_n  = 1'b1;
	  rd_n     = rd_i;
	  we_n     = 1'b1;
	end
	
      //----------------------------------------------------------------------
      // CSUB_I16
      //
      // Packed Q15 complex subtraction:
      //   result.real = rs1.real - rs2.real
      //   result.imag = rs1.imag - rs2.imag
      //
      // This instruction replaces the KISS FFT C_SUB macro.
      // ----------------------------------------------------------------------
      
	cvxif_instr_pkg::CSUB_I16: begin
	  // Unpack: {imag, real}
	  xr = registers_i[0][15:0];     // real
	  xi = registers_i[0][31:16];    // imag
	  wr = registers_i[1][15:0];     // real
	  wi = registers_i[1][31:16];    // imag

	  real_q15 = xr - wr;
	  imag_q15 = xi - wi;

	  result_n = {imag_q15, real_q15};

	  hartid_n = hartid_i;
	  id_n     = id_i;
	  valid_n  = 1'b1;
	  rd_n     = rd_i;
	  we_n     = 1'b1;
	end
	
      //----------------------------------------------------------------------
      // BFLY2_I16
      //
      // Fused radix-2 butterfly for one FFT lane.
      //
      // Software equivalent:
      //   t  = x1 * twiddle
      //   y0 = x0 + t
      //   y1 = x0 - t
      //
      // The twiddle is read from shadow_tw_q, previously written by SETTW_I16.
      // y0 is returned through the architectural destination register, while
      // y1 is stored in shadow_y1_q and later read by GETY1_I16.
      // ----------------------------------------------------------------------

	cvxif_instr_pkg::BFLY2_I16: begin
	  // unpack x0 (rs1)
	  x0r = registers_i[0][15:0];
	  x0i = registers_i[0][31:16];

	  // unpack x1 (rs2)
	  x1r = registers_i[1][15:0];
	  x1i = registers_i[1][31:16];

	  // unpack tw (shadow)
	  twr = shadow_tw_q[15:0];
	  twi = shadow_tw_q[31:16];

	  // t = x1 * tw
	  p1 = x1r * twr;
	  p2 = x1i * twi;
	  p3 = x1r * twi;
	  p4 = x1i * twr;
          /*
	  tr = (p1 - p2) >>> 15;
	  ti = (p3 + p4) >>> 15;*/
	  
	  tr_q30 = p1 - p2;
	  ti_q30 = p3 + p4;

	  tr = (tr_q30 + (1 <<< 14)) >>> 15;
	  ti = (ti_q30 + (1 <<< 14)) >>> 15;

	  // y0/y1
	  y0r = x0r + tr;  y0i = x0i + ti;
	  y1r = x0r - tr;  y1i = x0i - ti;

	  result_n    = {y0i, y0r};
	  shadow_y1_n = {y1i, y1r};

	  valid_n = 1'b1;
	  we_n    = 1'b1;
	  rd_n = rd_i;
	end
	
      // ----------------------------------------------------------------------
      // GETY1_I16
      //
      // Read the second output produced by the previous fused butterfly.
      // This avoids writing two architectural registers from one instruction
      // while keeping the control flow explicitly driven by software.
      // ----------------------------------------------------------------------
      
	cvxif_instr_pkg::GETY1_I16: begin
	  result_n = shadow_y1_q;

	  hartid_n = hartid_i;
	  id_n     = id_i;
	  valid_n  = 1'b1;
	  rd_n     = rd_i;
	  we_n     = 1'b1;
	end


      default: begin
        result_n = '0;
        hartid_n = '0;
        id_n     = '0;
        valid_n  = '0;
        rd_n     = '0;
        we_n     = '0;
      end
    endcase
  end

  always_ff @(posedge clk_i, negedge rst_ni) begin
    if (~rst_ni) begin
      result_q <= '0;
      hartid_q <= '0;
      id_q     <= '0;
      valid_q  <= '0;
      rd_q     <= '0;
      we_q     <= '0;
      shadow_tw_q <= '0;
      shadow_y1_q <= '0;
    end else begin
      result_q <= result_n;
      hartid_q <= hartid_n;
      id_q     <= id_n;
      valid_q  <= valid_n;
      rd_q     <= rd_n;
      we_q     <= we_n;
      shadow_tw_q <= shadow_tw_n;
      shadow_y1_q <= shadow_y1_n;
    end
  end

endmodule
