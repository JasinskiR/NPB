/*
MIT License

Copyright (c) 2021 Parallel Applications Modelling Group - GMAP 
	GMAP website: https://gmap.pucrs.br
	
	Pontifical Catholic University of Rio Grande do Sul (PUCRS)
	Av. Ipiranga, 6681, Porto Alegre - Brazil, 90619-900

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

------------------------------------------------------------------------------

The original NPB 3.4.1 version was written in Fortran and belongs to: 
	http://www.nas.nasa.gov/Software/NPB/

Authors of the Fortran code:
	P. O. Frederickson
	D. H. Bailey
	A. C. Woo
	H. Jin

------------------------------------------------------------------------------

The serial C++ version is a translation of the original NPB 3.4.1
Serial C++ version: https://github.com/GMAP/NPB-CPP/tree/master/NPB-SER

Authors of the C++ code: 
	Dalvan Griebler <dalvangriebler@gmail.com>
	Gabriell Araujo <hexenoften@gmail.com>
 	Júnior Löff <loffjh@gmail.com>

------------------------------------------------------------------------------

The OpenMP version is a parallel implementation of the serial C++ version
OpenMP version: https://github.com/GMAP/NPB-CPP/tree/master/NPB-OMP

Authors of the OpenMP code:
	Júnior Löff <loffjh@gmail.com>
	
*/

#include "omp.h"
#include "../common/npb-CPP.hpp"
#include "npbparams.hpp"

#define	MK 16
#define EPSILON 1.0e-8
#define	A 1220703125.0
#define	S 271828183.0

struct ProblemConfig {
    int m_val;
    double sx_verify_value;
    double sy_verify_value;
};

static ProblemConfig getProblemConfig(char class_npb) {
    ProblemConfig config;
    
    switch(class_npb) {
        case 'S':
        case 's':
            config.m_val = 24;
            config.sx_verify_value = -3.247834652034740e+3;
            config.sy_verify_value = -6.958407078382297e+3;
            break;
        case 'W':
        case 'w':
            config.m_val = 25;
            config.sx_verify_value = -2.863319731645753e+3;
            config.sy_verify_value = -6.320053679109499e+3;
            break;
        case 'A':
        case 'a':
            config.m_val = 28;
            config.sx_verify_value = -4.295875165629892e+3;
            config.sy_verify_value = -1.580732573678431e+4;
            break;
        case 'B':
        case 'b':
            config.m_val = 30;
            config.sx_verify_value = 4.033815542441498e+4;
            config.sy_verify_value = -2.660669192809235e+4;
            break;
        case 'C':
        case 'c':
            config.m_val = 32;
            config.sx_verify_value = 4.764367927995374e+4;
            config.sy_verify_value = -8.084072988043731e+4;
            break;
        case 'D':
        case 'd':
            config.m_val = 36;
            config.sx_verify_value = 1.982481200946593e+5;
            config.sy_verify_value = -1.020596636361769e+5;
            break;
        case 'E':
        case 'e':
            config.m_val = 40;
            config.sx_verify_value = -5.319717441530e+05;
            config.sy_verify_value = -3.688834557731e+05;
            break;
        default:
            printf("Unknown class: %c\n", class_npb);
            printf("Available classes: S, W, A, B, C, D, E\n");
            exit(EXIT_FAILURE);
    }
    return config;
}

static ProblemConfig config;

int main(int argc, char **argv){
    if(argc != 3) {
        printf("Usage: %s <class> <num_threads>\n", argv[0]);
        printf("Classes: S, W, A, B, C, D, E\n");
        printf("Example: %s B 4\n", argv[0]);
        return 1;
    }
    
    char class_npb = argv[1][0];
    int num_threads = atoi(argv[2]);
    
    if(num_threads <= 0) {
        printf("Number of threads must be positive\n");
        return 1;
    }
    
    config = getProblemConfig(class_npb);
    
    int MM = config.m_val - MK;
    int NN = 1 << MM;
    int NK = 1 << MK;
    int NQ = 10;
    int NK_PLUS = (2*NK)+1;
    
    double *x = (double*)malloc(sizeof(double) * NK_PLUS);
    double *q = (double*)malloc(sizeof(double) * NQ);
    
    if(!x || !q) {
        printf("Memory allocation failed\n");
        return 1;
    }
    
    omp_set_num_threads(num_threads);

	double  Mops, t1, t2, t3, t4, x1, x2;
	double  sx, sy, tm, an, tt, gc;
	double  sx_err, sy_err;
	int     np;
	int     i, ik, kk, l, k, nit;
	int     k_offset, j;
	boolean verified, timers_enabled;
	double  dum[3] = {1.0, 1.0, 1.0};
	char    size[16];

	FILE* fp;
	if((fp = fopen("timer.flag", "r"))==NULL){
		timers_enabled = FALSE;
	}else{
		timers_enabled = TRUE;
		fclose(fp);
	}

	sprintf(size, "%15.0f", pow(2.0, config.m_val+1));
	j = 14;
	if(size[j]=='.'){j--;}
	size[j+1] = '\0';
	printf("\n\n NAS Parallel Benchmarks 4.1 Parallel C++ version with OpenMP - EP Benchmark\n\n");
	printf(" Number of random numbers generated: %15s\n", size);
	printf(" Class: %c\n", class_npb);
	printf(" Number of threads: %d\n", num_threads);

	verified = FALSE;

	np = NN;

	vranlc(0, &dum[0], dum[1], &dum[2]);
	dum[0] = randlc(&dum[1], dum[2]);
	for(i=0; i<NK_PLUS; i++){x[i] = -1.0e99;}
	Mops = log(sqrt(fabs(max(1.0, 1.0))));

	timer_clear(0);
	timer_clear(1);
	timer_clear(2);
	timer_start(0);

	t1 = A;
	vranlc(0, &t1, A, x);

	t1 = A;

	for(i=0; i<MK+1; i++){
		t2 = randlc(&t1, t1);
	}

	an = t1;
	tt = S;
	gc = 0.0;
	sx = 0.0;
	sy = 0.0;

	for(i=0; i<=NQ-1; i++){
		q[i] = 0.0;
	}

	k_offset = -1;

    #pragma omp parallel
    {
        double t1, t2, t3, t4, x1, x2;
        int kk, i, ik, l;
        double *qq = (double*)malloc(sizeof(double) * NQ);
        double *x_private = (double*)malloc(sizeof(double) * NK_PLUS);

        for (i = 0; i < NQ; i++) qq[i] = 0.0;

       	#pragma omp for reduction(+:sx,sy)
		for(k=1; k<=np; k++){
			kk = k_offset + k;
			t1 = S;
			t2 = an;
			int thread_id = omp_get_thread_num();

			for(i=1; i<=100; i++){
				ik = kk / 2;
				if((2*ik)!=kk){t3=randlc(&t1,t2);}
				if(ik==0){break;}
				t3=randlc(&t2,t2);
				kk=ik;
			}

			if(timers_enabled && thread_id==0){timer_start(2);}
			vranlc(2*NK, &t1, A, x_private);
			if(timers_enabled && thread_id==0){timer_stop(2);}
			
			if(timers_enabled && thread_id==0){timer_start(1);}
			for(i=0; i<NK; i++){
				x1 = 2.0 * x_private[2*i] - 1.0;
				x2 = 2.0 * x_private[2*i+1] - 1.0;
				t1 = pow2(x1) + pow2(x2);
				if(t1 <= 1.0){
					t2 = sqrt(-2.0 * log(t1) / t1);
					t3 = (x1 * t2);
					t4 = (x2 * t2);
					l = max(fabs(t3), fabs(t4));
					qq[l] += 1.0;
					sx = sx + t3;
					sy = sy + t4;
				}
			}
			if(timers_enabled && thread_id==0){timer_stop(1);}
		}

		#pragma omp critical
        {
            for (i = 0; i <= NQ - 1; i++) q[i] += qq[i];
        }
        
        free(qq);
        free(x_private);
	}

	for(i=0; i<=NQ-1; i++){
		gc = gc + q[i];
	}

	timer_stop(0);
	tm = timer_read(0);

	nit = 0;
	verified = TRUE;
	
	if(verified){
		sx_err = fabs((sx - config.sx_verify_value) / config.sx_verify_value);
		sy_err = fabs((sy - config.sy_verify_value) / config.sy_verify_value);
		verified = ((sx_err <= EPSILON) && (sy_err <= EPSILON));
	}
	Mops = pow(2.0, config.m_val+1)/tm/1000000.0;

	printf("\n EP Benchmark Results:\n\n");
	printf(" CPU Time =%10.4f\n", tm);
	printf(" N = 2^%5d\n", config.m_val);
	printf(" No. Gaussian Pairs = %15.0f\n", gc);
	printf(" Sums = %25.15e %25.15e\n", sx, sy);
	printf(" Counts: \n");
	for(i=0; i<NQ-1; i++){
		printf("%3d%15.0f\n", i, q[i]);
	}

	char num_threads_str[16];
	sprintf(num_threads_str, "%d", num_threads);

	c_print_results((char*)"EP",
			class_npb,
			config.m_val+1,
			0,
			0,
			nit,
			tm,
			Mops,
			(char*)"Random numbers generated",
			verified,
			(char*)NPBVERSION,
			(char*)COMPILETIME,
			(char*)COMPILERVERSION,
			(char*)LIBVERSION,
			num_threads_str,
			(char*)CS1,
			(char*)CS2,
			(char*)CS3,
			(char*)CS4,
			(char*)CS5,
			(char*)CS6,
			(char*)CS7);

	if(timers_enabled){
		if(tm <= 0.0){tm = 1.0;}
		tt = timer_read(0);
		printf("\nTotal time:     %9.3f (%6.2f)\n", tt, tt*100.0/tm);
		tt = timer_read(1);
		printf("Gaussian pairs: %9.3f (%6.2f)\n", tt, tt*100.0/tm);
		tt = timer_read(2);
		printf("Random numbers: %9.3f (%6.2f)\n", tt, tt*100.0/tm);
	}

	free(x);
	free(q);

	return 0;
}
