all: pwm_pulse

pwm_pulse: 
	gcc -o pwm_pulse pwm_pulse.c
nanopulse: 
	gcc -o nanopulse nanopulse.c

clean:
	rm pwm_pulse nanopulse

