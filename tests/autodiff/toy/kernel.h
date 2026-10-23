#pragma once

// C side of the toy: a weighted square, in its own file so that the derivative needs the link-time view of the program.
double kernel_weighted_square(double weight, double x);
