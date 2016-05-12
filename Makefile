all: pwm_pulse

pwm_pulse: 
	gcc -o pwm_pulse pwm_pulse.c

clean:
	rm pwm_pulse

