function crossover_quant = crossover_zpk_quant(lowpass, highpass);

addpath ./../eq

if length(lowpass) != length(highpass)
	error("length of lowpass and highpass array do not match");
end

n = length(lowpass);
crossover_quant.lp_coef = cell(1,n);
crossover_quant.hp_coef = cell(1,n);

for i = 1:n
	lp_eq = lowpass(i);
	hp_eq = highpass(i);
	crossover_quant.lp_coef(i) = eq_iir_blob_quant(lp_eq.p_z, lp_eq.p_p, lp_eq.p_k)(7:13);
 	crossover_quant.hp_coef(i) = eq_iir_blob_quant(hp_eq.p_z, hp_eq.p_p, hp_eq.p_k)(7:13);
end

crossover_quant.lp_coef = cell2mat(crossover_quant.lp_coef);
crossover_quant.hp_coef = cell2mat(crossover_quant.hp_coef);

rmpath ./../eq
end
