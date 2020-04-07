function config = crossover_generate_config(crossover_bqs, num_sinks, assign_sinks);

config.num_sinks = num_sinks;
config.assign_sinks = assign_sinks;
% Interleave the coefficients for the low and high pass filters
n = num_sinks - 1;
j = 1;
k = 1;
for i = 1:n
	config.all_coef(k:k+6) = crossover_bqs.lp_coef(j:j+6); k = k+7;
	config.all_coef(k:k+6) = crossover_bqs.hp_coef(j:j+6); k = k+7;
	j = j+7;
end
end
