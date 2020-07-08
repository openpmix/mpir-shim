/*
 * Copyright (c) 2020      IBM Corporation.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 * Functions to assist regression testing
 */

#ifndef MPIRSHIM_TEST_H
#define MPIRSHIM_TEST_H
#ifdef MPIR_SHIM_TESTCASE

/**
 * @name   MPIR_Shim_release_application
 * @brief  Release application processes from hold in MPI_Init so they may
 *         contine execution.
 * @return STATUS_OK if successful, STATUS_FAIL otherwise
 */
int MPIR_Shim_release_application(void);

#endif /* MPIR_SHIM_TESTCASE */
#endif /* MPIRSHIM_TEST_H */
