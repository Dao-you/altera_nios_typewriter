module hw03_Mux41(I3,I2,S1,S0,I1,I0,Y);
input I3,I2,I1,I0,S1,S0;
output Y;

wire	I00,I01,I02,I03;
wire	S0bar,S1bar;

assign	S1bar =  ~S1;
assign	S0bar =  ~S0;
assign	I00 = I0 & S1bar & S0bar;
assign	I01 = I1 & S1bar & S0;
assign	I02 = I2 & S1 & S0bar;
assign	I03 = I3 & S1 & S0;
assign	Y = I03 | I02 | I01 | I00;

endmodule
