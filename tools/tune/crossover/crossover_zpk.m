function crossover = crossover_zpk(fs, fc_low, fc_mid, fc_high);

addpath ./../eq/
switch nargin
	case 1, crossover = crossover_generate_pass(fs);
	case 2, crossover = crossover_generate_2way(fs, fc_low);
	case 3, crossover = crossover_generate_3way(fs, fc_low, fc_mid);
	case 4, crossover = crossover_generate_4way(fs, fc_low, fc_mid, fc_high);
	otherwise, error("Invalid number of arguments");
end
rmpath ./../eq
end

function crossover_pass = crossover_generate_pass(fs);
	error("Not implemented yet")
end

function crossover_2way = crossover_generate_2way(fs, fc);
	crossover_2way.lp = [lp_iir(fs, fc, 0)];
	crossover_2way.hp = [hp_iir(fs, fc, 0)];
end

function crossover_3way = crossover_generate_3way(fs, fc_low, fc_high);
	crossover_3way.lp = [lp_iir(fs, fc_low, 0), lp_iir(fs, fc_high, 0)];
	crossover_3way.hp = [hp_iir(fs, fc_low, 0), hp_iir(fs, fc_high, 0)];
end

function crossover_4way = crossover_generate_4way(fs, fc_low, fc_mid, fc_high);
	crossover_4way.lp = [lp_iir(fs, fc_low, 0), lp_iir(fs, fc_mid, 0), lp_iir(fs, fc_high, 0)];
	crossover_4way.hp = [hp_iir(fs, fc_low, 0), lp_iir(fs, fc_mid, 0), hp_iir(fs, fc_high, 0)];
end

% Generate the zpk coefficients for a second order
% low pass butterworth filter
function lp = lp_iir(fs, fc, gain_db)
% Get defaults for equalizer design
lp = eq_defaults();
lp.fs = fs;
lp.enable_iir = 1;
lp.norm_type = '1k';
lp.norm_offs_db = gain_db;

% Design
lp.peq = [ lp.PEQ_LP2 fc NaN NaN ];
lp = eq_compute(lp);
% eq_plot(lp);
end

% Generate the zpk coefficients for a second order
% low pass butterworth filter
function hp = hp_iir(fs, fc, gain_db)
% Get defaults for equalizer design
hp = eq_defaults();
hp.fs = fs;
hp.enable_iir = 1;
hp.norm_type = '1k';
hp.norm_offs_db = gain_db;

% Design
hp.peq = [ hp.PEQ_HP2 fc NaN NaN ];
hp = eq_compute(hp);
% eq_plot(hp);
end
