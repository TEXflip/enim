#!/usr/bin/python3
import numpy as np
import sympy as sp

I_w, I_h, W_w, W_h, Mx, My = sp.symbols('I_w I_h W_w W_h Mx My')

W_UV = sp.Matrix([
    [W_w/2, 0, 0],
    [0, W_h/2, 0],
    [0, 0, 1]
])

T_W = sp.Matrix([
    [1/I_w, 0, 0],
    [0, 1/I_h, 0],
    [0, 0, 1]
])

M = W_UV @ T_W

sp.pprint(M, use_unicode=True)