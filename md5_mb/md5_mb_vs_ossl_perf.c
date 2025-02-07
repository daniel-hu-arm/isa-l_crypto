/**********************************************************************
  Copyright(c) 2011-2016 Intel Corporation All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <openssl/evp.h>
#include "md5_mb.h"
#include "test.h"

// Set number of outstanding jobs
#define TEST_BUFS 32

#ifdef CACHED_TEST
// Loop many times over same data
#  define TEST_LEN     4*1024
#  define TEST_LOOPS   10000
#  define TEST_TYPE_STR "_warm"
#else
// Uncached test.  Pull from large mem base.
#  define GT_L3_CACHE  32*1024*1024	/* some number > last level cache */
#  define TEST_LEN     (GT_L3_CACHE / TEST_BUFS)
#  define TEST_LOOPS   100
#  define TEST_TYPE_STR "_cold"
#endif

#define TEST_MEM TEST_LEN * TEST_BUFS * TEST_LOOPS

unsigned char md5_ossl(const uint8_t * d, unsigned long n, uint8_t * md)
{
	unsigned int tmplen;
	EVP_MD_CTX *c;

	c = EVP_MD_CTX_create();
	EVP_DigestInit_ex(c, EVP_md5(), NULL);
	EVP_DigestUpdate(c, d, n);
	EVP_DigestFinal_ex(c, md, &tmplen);
	EVP_MD_CTX_destroy(c);
	return 1;
}

/* Reference digest global to reduce stack usage */
static uint8_t digest_ssl[TEST_BUFS][4 * MD5_DIGEST_NWORDS];

int main(void)
{
	int ret;
	MD5_HASH_CTX_MGR *mgr = NULL;
	MD5_HASH_CTX ctxpool[TEST_BUFS];
	unsigned char *bufs[TEST_BUFS];
	uint32_t i, j, t, fail = 0;
	struct perf start, stop;

	for (i = 0; i < TEST_BUFS; i++) {
		bufs[i] = (unsigned char *)calloc((size_t)TEST_LEN, 1);
		if (bufs[i] == NULL) {
			printf("calloc failed test aborted\n");
			return 1;
		}
		// Init ctx contents
		hash_ctx_init(&ctxpool[i]);
		ctxpool[i].user_data = (void *)((uint64_t) i);
	}

	ret = posix_memalign((void *)&mgr, 16, sizeof(MD5_HASH_CTX_MGR));
	if (ret) {
		printf("alloc error: Fail");
		return -1;
	}
	md5_ctx_mgr_init(mgr);

	// Start OpenSSL tests
	perf_start(&start);
	for (t = 0; t < TEST_LOOPS; t++) {
		for (i = 0; i < TEST_BUFS; i++)
			md5_ossl(bufs[i], TEST_LEN, digest_ssl[i]);
	}
	perf_stop(&stop);

	printf("md5_openssl" TEST_TYPE_STR ": ");
	perf_print(stop, start, (long long)TEST_LEN * i * t);

	// Start mb tests
	perf_start(&start);
	for (t = 0; t < TEST_LOOPS; t++) {
		for (i = 0; i < TEST_BUFS; i++)
			md5_ctx_mgr_submit(mgr, &ctxpool[i], bufs[i], TEST_LEN, HASH_ENTIRE);

		while (md5_ctx_mgr_flush(mgr)) ;
	}
	perf_stop(&stop);

	printf("multibinary_md5" TEST_TYPE_STR ": ");
	perf_print(stop, start, (long long)TEST_LEN * i * t);

	for (i = 0; i < TEST_BUFS; i++) {
		for (j = 0; j < MD5_DIGEST_NWORDS; j++) {
			if (ctxpool[i].job.result_digest[j] !=
			    to_le32(((uint32_t *) digest_ssl[i])[j])) {
				fail++;
				printf("Test%d, digest%d fail %08X <=> %08X\n",
				       i, j, ctxpool[i].job.result_digest[j],
				       to_le32(((uint32_t *) digest_ssl[i])[j]));
			}
		}
	}

	printf("Multi-buffer md5 test complete %d buffers of %d B with "
	       "%d iterations\n", TEST_BUFS, TEST_LEN, TEST_LOOPS);

	if (fail)
		printf("Test failed function check %d\n", fail);
	else
		printf(" multibinary_md5_ossl_perf: Pass\n");

	return fail;
}
