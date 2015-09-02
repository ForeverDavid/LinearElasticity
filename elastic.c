#include "elastic.h"
#include "ls_calls.h"


static void excfun(int sigid) {
  fprintf(stdout,"\n Unexpected error:");  fflush(stdout);
  switch(sigid) {
    case SIGABRT:
      fprintf(stdout,"  Abnormal stop\n");  break;
    case SIGBUS:
      fprintf(stdout,"  Code error...\n");  break;
    case SIGFPE:
      fprintf(stdout,"  Floating-point exception\n"); break;
    case SIGILL:
      fprintf(stdout,"  Illegal instruction\n"); break;
    case SIGSEGV:
      fprintf(stdout,"  Segmentation fault.\n");  break;
    case SIGTERM:
    case SIGINT:
      fprintf(stdout,"  Programm killed.\n");  break;
  }
  fprintf(stdout," No data file saved.\n"); 
  exit(1);
}

static void usage(char *prog) {
  fprintf(stdout,"\n usage: %s [-v[n]] [-h] [opts..] filein[.mesh]\n",prog);

  fprintf(stdout,"\n** Generic options :\n");
  fprintf(stdout,"-d       Turn on debug mode\n");
  fprintf(stdout,"-h       Print this message\n");
  fprintf(stdout,"-v [n]   Tune level of verbosity\n");
  fprintf(stdout,"-t n     FE type: 1 (P1), 2 (P2)\n");

  fprintf(stdout,"\n**  File specifications\n");
  fprintf(stdout,"-in  file  input triangulation\n");
  fprintf(stdout,"-sol file  load initial solution\n");

  fprintf(stdout,"\n** Parameters\n");
  fprintf(stdout,"-err e   accuracy (default %E)\n",LS_RES);
  fprintf(stdout,"-nit n   iterations (default %d)\n",LS_MAXIT);

  exit(1);
}

static int parsar(int argc,char *argv[],LSst *lsst) {
  int      i;
  char    *ptr;

  i = 1;
  while ( i < argc ) {
    if ( *argv[i] == '-' ) {
      switch(argv[i][1]) {
      case 'h':  /* on-line help */
      case '?':
        usage(argv[0]);
        break;
      case 'd':  /* debug */
        lsst->info.ddebug = 1;
        break;
      case 'e':
        if ( !strcmp(argv[i],"-err") ) {
          ++i;
          if ( isdigit(argv[i][0]) )
            lsst->sol.err = strtod(argv[i],NULL);
          else
            --i; 
        }
        break;
      case 'i':
        if ( !strcmp(argv[i],"-in") ) {
          ++i;
          lsst->mesh.name = argv[i];
        }
        break;
      case 'n':
        if ( !strcmp(argv[i],"-nit") ) {
          ++i;
          if ( i < argc && isdigit(argv[i][0]) )
            lsst->sol.nit = atoi(argv[i]);
          else
            --i; 
        }
        break;
      case 's':
        if ( !strcmp(argv[i],"-sol") ) {
          ++i;
          lsst->sol.namein = argv[i];
          ptr = strstr(lsst->sol.namein,".sol");
          if ( !ptr )  strcat(lsst->sol.namein,".sol");
        }
        break;
      case 't':
        if ( ++i < argc ) {
          if ( isdigit(argv[i][0]) )
            lsst->info.typ = atoi(argv[i]);
          else { 
            fprintf(stderr,"Missing argument option %c\n",argv[i-1][1]);
            usage(argv[0]);
            i--;
          }
        }
        else {
          fprintf(stderr,"Missing argument option %c\n",argv[i-1][1]);
          usage(argv[0]);
        }
        break;
      case 'v':
        if ( ++i < argc ) {
          if ( argv[i][0] == '-' || isdigit(argv[i][0]) )
            lsst->info.imprim = atoi(argv[i]);
          else 
            i--;
        }
        else {
          fprintf(stderr,"Missing argument option %c\n",argv[i-1][1]);
          usage(argv[0]);
        }
        break;
      default:
        fprintf(stderr,"  Unrecognized option %s\n",argv[i]);
        usage(argv[0]);
      }
    }
    else {
      if ( lsst->mesh.name == NULL ) {
        lsst->mesh.name = argv[i];
        if ( lsst->info.imprim == -99 )  lsst->info.imprim = 5;
      }
      else {
        fprintf(stdout,"  Argument %s ignored\n",argv[i]);
        usage(argv[0]);
      }
    }
    i++;
  }

  /* check params */
  if ( lsst->info.imprim == -99 ) {
    fprintf(stdout,"\n  -- PRINT (0 10(advised) -10) ?\n");
    fflush(stdin);
    fscanf(stdin,"%d",&i);
    lsst->info.imprim = i;
  }

  if ( lsst->mesh.name == NULL ) {
    lsst->mesh.name = (char *)calloc(128,sizeof(char));
    assert(lsst->mesh.name);
    fprintf(stdout,"  -- MESH BASENAME ?\n");
    fflush(stdin);
    fscanf(stdin,"%s",lsst->mesh.name);
  }
  if ( !lsst->sol.nameout ) {
    lsst->sol.nameout = (char *)calloc(128,sizeof(char));
    assert(lsst->sol.nameout);
    strcpy(lsst->sol.nameout,lsst->mesh.name);
    ptr = strstr(lsst->sol.nameout,".mesh");
    if ( ptr ) *ptr = '\0';
    strcat(lsst->sol.nameout,".sol");
  }

  return(1);
}

static int parsop(LSst *lsst) {
  Cl         *pcl;
  Mat        *pm;
  float       fp1,fp2;
  int         i,j,ncld,ret;
  char       *ptr,buf[256],data[256];
  FILE       *in;

  strcpy(data,lsst->mesh.name);
  ptr = strstr(data,".mesh");
  if ( ptr )  *ptr = '\0';
  strcat(data,".elas");
  in = fopen(data,"r");
  if ( !in ) {
    sprintf(data,"%s","DEFAULT.elas");
    in = fopen(data,"r");
    if ( !in )  return(1);
  }
  fprintf(stdout,"  %%%% %s OPENED\n",data);

  /* read parameters */
  lsst->sol.nbcl = 0;
  while ( !feof(in) ) {
    ret = fscanf(in,"%s",data);
    if ( !ret || feof(in) )  break;
    for (i=0; i<strlen(data); i++) data[i] = tolower(data[i]);

    /* check for condition type */
    if ( !strcmp(data,"dirichlet")  || !strcmp(data,"load") ) {
      fscanf(in,"%d",&ncld);
      for (i=lsst->sol.nbcl; i<lsst->sol.nbcl+ncld; i++) {
        pcl = &lsst->sol.cl[i];
        if ( !strcmp(data,"load") )             pcl->typ = Load;
        else  if ( !strcmp(data,"dirichlet") )  pcl->typ = Dirichlet;
        else {
          fprintf(stdout,"  %%%% Unknown condition: %s\n",data);
          continue;
        }

        /* check for entity */
        fscanf(in,"%d %s ",&pcl->ref,buf);
        for (j=0; j<strlen(buf); j++)  buf[j] = tolower(buf[j]);
        fscanf(in,"%c",&pcl->att);
        pcl->att = tolower(pcl->att);
        if ( (pcl->typ == Dirichlet) && (pcl->att != 'v' && pcl->att != 'f') ) {
          fprintf(stdout,"  %%%% Wrong format: %s\n",buf);
          continue;
        }
        else if ( (pcl->typ == Load) && (pcl->att != 'v' && pcl->att != 'f' && pcl->att != 'n') ) {
          fprintf(stdout,"  %%%% Wrong format: %s\n",buf);
          continue;
        }
        if ( !strcmp(buf,"vertices") || !strcmp(buf,"vertex") )          pcl->elt = LS_ver;
        else if ( !strcmp(buf,"edges") || !strcmp(buf,"edge") )          pcl->elt = LS_edg;
        else if ( !strcmp(buf,"triangles") || !strcmp(buf,"triangle") )  pcl->elt = LS_tri;

        if ( pcl->att != 'f' && pcl->att != 'n' ) {
          for (j=0; j<lsst->info.dim; j++) {
            fscanf(in,"%f ",&fp1);
            pcl->u[j] = fp1;
          }
        }
        else if ( pcl->att == 'n' ) {
          fscanf(in,"%f ",&fp1);
          pcl->u[0] = fp1;
        }
      }
      lsst->sol.nbcl += ncld;
    }
    /* gravity or body force */
    else if ( !strcmp(data,"gravity") ) {
      lsst->info.load |= (1 << 0);
      for (j=0; j<lsst->info.dim; j++) {
        fscanf(in,"%f ",&fp1);
        lsst->info.gr[j] = fp1;
      }
    }
    else if ( !strcmp(data,"lame") ) {
      fscanf(in,"%d",&ncld);
      assert(ncld <= LS_MAT);
      lsst->sol.nmat = ncld;
      for (i=0; i<ncld; i++) {
        pm = &lsst->sol.mat[i];
        fscanf(in,"%d %f %f\n",&pm->ref,&fp1,&fp2);
        pm->lambda = fp1;
        pm->mu     = fp2;
      }
    }
    else if ( !strcmp(data,"youngpoisson") ) {
      fscanf(in,"%d",&ncld);
      lsst->sol.nmat = ncld;
      for (i=0; i<ncld; i++) {
        pm = &lsst->sol.mat[i];
        fscanf(in,"%d %f %f\n",&pm->ref,&fp1,&fp2);
        pm->lambda = (fp1 * fp2) / ((1.0+fp2) * (1.0-2.0*fp2));
        pm->mu     = fp1 / (2.0*( 1.0+fp2));
      }
    }
  }
  fclose(in);

  for (i=0; i<lsst->sol.nbcl; i++) {
    pcl = &lsst->sol.cl[i];
		lsst->sol.cltyp |= pcl->elt;
  }

  return(1);
}

int main(int argc,char **argv) {
  LSst     lsst;
	int      ier;
	char     stim[32];

  fprintf(stdout,"  -- ELASTIC, Release %s (%s) \n     %s\n",LS_VER,LS_REL,LS_CPY);
  tminit(lsst.info.ctim,TIMEMAX);
  chrono(ON,&lsst.info.ctim[0]);

  /* trap exceptions */
  signal(SIGABRT,excfun);
  signal(SIGFPE,excfun);
  signal(SIGILL,excfun);
  signal(SIGSEGV,excfun);
  signal(SIGTERM,excfun);
  signal(SIGINT,excfun);
  signal(SIGBUS,excfun);

  /* init structure */
  memset(&lsst.mesh,0,sizeof(Mesh));
  memset(&lsst.sol,0,sizeof(Sol));
	lsst.sol.cl  = (Cl*)calloc(LS_CL,sizeof(Cl));
  lsst.sol.mat = (Mat*)calloc(LS_MAT,sizeof(Mat));
  lsst.sol.err = LS_RES;
  lsst.sol.nit = LS_MAXIT;

  /* global parameters */
  lsst.info.dim    = 3;
	lsst.info.ver    = 1;
  lsst.info.imprim = -99;
  lsst.info.ddebug = 0;
  lsst.info.zip    = 0;
  lsst.info.typ    = P1;
  lsst.info.mfree  = 1;

  /* parse command line */
  if ( !parsar(argc,argv,&lsst) )  return(1);

  /* loading date */
  if ( lsst.info.imprim )   fprintf(stdout,"\n  -- INPUT DATA\n");
  chrono(ON,&lsst.info.ctim[1]);

  /* loading mesh */
  ier = loadMesh(&lsst);
	if ( ier <=0 )  return(1);

  /* set function pointer */
  if ( lsst.info.dim == 2 ) {
    elasti1 = elasti1_2d;
    hashar  = hashar_2d;
    pack    = pack_2d;
  }
  else {
    elasti1 = elasti1_3d;
    hashar  = hashar_3d;
    pack    = pack_3d;
  }

  /* counting P2 nodes */
	if ( lsst.info.typ == P2 )  lsst.info.np2 = hashar(&lsst);

  /* allocating memory */
  if ( !lsst.sol.u ) {
    lsst.sol.u  = (double*)calloc(lsst.info.dim * (lsst.info.npi+lsst.info.np2),sizeof(double));
    assert(lsst.sol.u);
  }

  /* loading solution (or Dirichlet values) */
  ier = loadSol(&lsst);
  if ( !ier )  return(1);

  /* parse parameters in file */
  if ( !parsop(&lsst) )  return(1);
  if ( lsst.sol.nmat && !pack(&lsst) )  return(1);
	printim(lsst.info.ctim[1].gdif,stim);
  fprintf(stdout,"  -- DATA READING COMPLETED.     %s\n",stim);

  /* solve */
  chrono(ON,&lsst.info.ctim[2]);
  fprintf(stdout,"\n  %s\n   MODULE ELASTIC-LJLL : %s (%s)\n  %s\n",LS_STR,LS_VER,LS_REL,LS_STR);
  if ( lsst.info.imprim )   fprintf(stdout,"\n  -- PHASE 1 : SOLVING\n");

	ier = elasti1(&lsst);
	if ( !ier )  return(1);
  chrono(OFF,&lsst.info.ctim[2]);
  if ( lsst.info.imprim ) {
		printim(lsst.info.ctim[2].gdif,stim);
    fprintf(stdout,"  -- PHASE 1 COMPLETED.     %s\n",stim);
	}
  fprintf(stdout,"\n  %s\n   END OF MODULE ELASTIC-LJLL \n  %s\n",LS_STR,LS_STR);

  /* save file */
  if ( lsst.info.imprim )  fprintf(stdout,"\n  -- WRITING DATA FILE %s\n",lsst.mesh.name);
  chrono(ON,&lsst.info.ctim[1]);
  if ( lsst.info.zip && !unpack(&lsst) )  return(1);
  ier = saveSol(&lsst);
	if ( !ier )   return(1);
  chrono(OFF,&lsst.info.ctim[1]);
  if ( lsst.info.imprim )  fprintf(stdout,"  -- WRITING COMPLETED\n");

  /* free mem */
	free(lsst.sol.u);

  chrono(OFF,&lsst.info.ctim[0]);
  if ( lsst.info.imprim > 0 ) {
	  printim(lsst.info.ctim[0].gdif,stim);
    fprintf(stdout,"\n   ELAPSED TIME  %s\n",stim);
  }

  return(0);
}
