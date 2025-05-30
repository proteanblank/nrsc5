/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "conv.h"
#include "decode.h"
#include "input.h"
#include "pids.h"
#include "private.h"

/* 1012s.pdf figure 10-4 */
static int bl_delay[] = { 2, 1, 5 };
static int ml_delay[] = { 11, 6, 7 };
static int bu_delay[] = { 10, 8, 9 };
static int mu_delay[] = { 4, 3, 0 };
static int el_delay[] = { 0, 1 };
static int eu_delay[] = { 2, 3, 5, 4 };

/* 1012s.pdf figure 10-5 */
static int pids_il_delay[] = { 0, 1, 12, 13, 6, 5, 18, 17, 11, 7, 23, 19 };
static int pids_iu_delay[] = { 2, 4, 14, 16, 3, 8, 15, 20, 9, 10, 21, 22 };

static int bit_map(unsigned char matrix[PARTITION_WIDTH_AM * BLKSZ * 8], int b, int k, int p)
{
    int col = (9*k) % 25;
    int row = (11*col + 16*(k/25) + 11*(k/50)) % 32;
    return (matrix[PARTITION_WIDTH_AM * (b*BLKSZ + row) + col] >> p) & 1;
}

static void interleaver_ma1(decode_t *st)
{
    int b, k, p;
    for (int n = 0; n < 18000; n++)
    {
        b = n/2250;
        k = (n + n/750 + 1) % 750;
        p = n % 3;
        st->bl[n] = bit_map(st->buffer_pl, b, k, p);

        b = (3*n + 3) % 8;
        k = (n + n/3000 + 3) % 750;
        p = 3 + (n % 3);
        st->ml[DIVERSITY_DELAY_AM + n] = bit_map(st->buffer_pl, b, k, p);

        b = n/2250;
        k = (n + n/750) % 750;
        p = n % 3;
        st->bu[n] = bit_map(st->buffer_pu, b, k, p);

        b = (3*n) % 8;
        k = (n + n/3000 + 2) % 750;
        p = 3 + (n % 3);
        st->mu[DIVERSITY_DELAY_AM + n] = bit_map(st->buffer_pu, b, k, p);
    }

    if (st->input->sync.psmi != SERVICE_MODE_MA3)
    {
        for (int n = 0; n < 12000; n++)
        {
            b = (3*n + n/3000) % 8;
            k = (n + (n/6000)) % 750;
            p = n % 2;
            st->el[n] = bit_map(st->buffer_t, b, k, p);
        }
        for (int n = 0; n < 24000; n++)
        {
            b = (3*n + n/3000 + 2*(n/12000)) % 8;
            k = (n + (n/6000)) % 750;
            p = n % 4;
            st->eu[n] = bit_map(st->buffer_s, b, k, p);
        }
    }
    else
    {
        for (int n = 0; n < 18000; n++)
        {
            b = (3*n + 3) % 8;
            k = (n + n/3000 + 3) % 750;
            p = n % 3;
            st->ebl[n] = bit_map(st->buffer_t, b, k, p);

            b = (3*n + 3) % 8;
            k = (n + n/3000 + 3) % 750;
            p = 3 + (n % 3);
            st->eml[DIVERSITY_DELAY_AM + n] = bit_map(st->buffer_t, b, k, p);

            b = (3*n) % 8;
            k = (n + n/3000 + 2) % 750;
            p = n % 3;
            st->ebu[n] = bit_map(st->buffer_s, b, k, p);

            b = (3*n) % 8;
            k = (n + n/3000 + 2) % 750;
            p = 3 + (n % 3);
            st->emu[DIVERSITY_DELAY_AM + n] = bit_map(st->buffer_s, b, k, p);
        }
    }

    for (int i = 0; i < 6000; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            st->p1_am[i*12 + bl_delay[j]] = st->bl[i*3 + j];
            st->p1_am[i*12 + ml_delay[j]] = st->ml[i*3 + j];
            st->p1_am[i*12 + bu_delay[j]] = st->bu[i*3 + j];
            st->p1_am[i*12 + mu_delay[j]] = st->mu[i*3 + j];
        }
        if (st->input->sync.psmi != SERVICE_MODE_MA3)
        {
            for (int j = 0; j < 2; j++)
            {
                st->p3_am[i*6 + el_delay[j]] = st->el[i*2 + j];
            }
            for (int j = 0; j < 4; j++)
            {
                st->p3_am[i*6 + eu_delay[j]] = st->eu[i*4 + j];
            }
        }
        else
        {
            for (int j = 0; j < 3; j++)
            {
                st->p3_am[i*12 + bl_delay[j]] = st->ebl[i*3 + j];
                st->p3_am[i*12 + ml_delay[j]] = st->eml[i*3 + j];
                st->p3_am[i*12 + bu_delay[j]] = st->ebu[i*3 + j];
                st->p3_am[i*12 + mu_delay[j]] = st->emu[i*3 + j];
            }
        }
    }

    memmove(st->ml, st->ml + 18000, DIVERSITY_DELAY_AM);
    memmove(st->mu, st->mu + 18000, DIVERSITY_DELAY_AM);
    if (st->input->sync.psmi == SERVICE_MODE_MA3)
    {
        memmove(st->eml, st->eml + 18000, DIVERSITY_DELAY_AM);
        memmove(st->emu, st->emu + 18000, DIVERSITY_DELAY_AM);
    }

    int offset = 0;
    for (int i = 0; i < 8 * P1_FRAME_LEN_AM * 3; i++)
    {
        switch (i % 15)
        {
        case 1:
        case 4:
        case 7:
            st->viterbi_p1_am[i] = 0;
            break;
        default:
            st->viterbi_p1_am[i] = st->p1_am[offset++] ? 1 : -1;
        }
    }

    offset = 0;
    if (st->input->sync.psmi != SERVICE_MODE_MA3)
    {
        for (int i = 0; i < P3_FRAME_LEN_MA1 * 3; i++)
        {
            switch (i % 6)
            {
            case 1:
            case 4:
            case 5:
                st->viterbi_p3_am[i] = 0;
                break;
            default:
                st->viterbi_p3_am[i] = st->p3_am[offset++] ? 1 : -1;
            }
        }
    }
    else
    {
        for (int i = 0; i < P3_FRAME_LEN_MA3 * 3; i++)
        {
            switch (i % 15)
            {
            case 1:
            case 4:
            case 7:
                st->viterbi_p3_am[i] = 0;
                break;
            default:
                st->viterbi_p3_am[i] = st->p3_am[offset++] ? 1 : -1;
            }
        }
    }
}

// calculate number of bit errors by re-encoding and comparing to the input
static int bit_errors(int8_t *coded, uint8_t *decoded, unsigned int k, unsigned int frame_len,
                      unsigned int g1, unsigned int g2, unsigned int g3,
                      uint8_t *puncture, int puncture_len)
{
    uint16_t r = 0;
    unsigned int i, j, errors = 0;

    // tail biting
    for (i = 0; i < (k-1); i++)
        r = (r >> 1) | (decoded[frame_len - (k-1) + i] << (k-1));

    for (i = 0, j = 0; i < frame_len; i++, j += 3)
    {
        // shift in new bit
        r = (r >> 1) | (decoded[i] << (k-1));

        if (puncture[j % puncture_len] && ((coded[j] > 0) != __builtin_parity(r & g1)))
            errors++;
        if (puncture[(j+1) % puncture_len] && ((coded[j+1] > 0) != __builtin_parity(r & g2)))
            errors++;
        if (puncture[(j+2) % puncture_len] && ((coded[j+2] > 0) != __builtin_parity(r & g3)))
            errors++;
    }

    return errors;
}

static int bit_errors_p1_fm(int8_t *coded, uint8_t *decoded)
{
    uint8_t puncture[] = {1, 1, 1, 1, 1, 0};
    return bit_errors(coded, decoded, 7, P1_FRAME_LEN_FM, 0133, 0171, 0165, puncture, 6);
}

static int bit_errors_p1_am(int8_t *coded, uint8_t *decoded)
{
    uint8_t puncture[] = {1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1};
    return bit_errors(coded, decoded, 9, P1_FRAME_LEN_AM, 0561, 0657, 0711, puncture, 15);
}

static int bit_errors_p3_ma1(int8_t *coded, uint8_t *decoded)
{
    uint8_t puncture[] = {1, 0, 1, 1, 0, 0};
    return bit_errors(coded, decoded, 9, P3_FRAME_LEN_MA1, 0561, 0753, 0711, puncture, 6);
}

static int bit_errors_p3_ma3(int8_t *coded, uint8_t *decoded)
{
    uint8_t puncture[] = {1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1};
    return bit_errors(coded, decoded, 9, P3_FRAME_LEN_MA3, 0561, 0657, 0711, puncture, 15);
}

static void descramble(uint8_t *buf, unsigned int length)
{
    const unsigned int width = 11;
    unsigned int i, val = 0x3ff;
    for (i = 0; i < length; i += 8)
    {
        unsigned int j;
        for (j = 0; j < 8; ++j)
        {
            int bit = ((val >> 9) ^ val) & 1;
            val |= bit << width;
            val >>= 1;
            buf[i + j] ^= bit;
        }
    }
}

void decode_process_p1(decode_t *st)
{
    const int J = 20, B = 16, C = 36;
    const int8_t v[] = {
        10, 2, 18, 6, 14, 8, 16, 0, 12, 4,
        11, 3, 19, 7, 15, 9, 17, 1, 13, 5
    };
    unsigned int i, out = 0;
    for (i = 0; i < P1_FRAME_LEN_ENCODED_FM; i++)
    {
        int partition = v[i % J];
        int block = ((i / J) + (partition * 7)) % B;
        int k = i / (J * B);
        int row = (k * 11) % 32;
        int column = (k * 11 + k / (32*9)) % C;
        st->viterbi_p1[out++] = st->buffer_pm[(block * 32 + row) * 720 + partition * C + column];
        if ((out % 6) == 5) // depuncture, [1, 1, 1, 1, 1, 0]
            st->viterbi_p1[out++] = 0;
    }

    nrsc5_conv_decode_p1(st->viterbi_p1, st->scrambler_p1);
    nrsc5_report_ber(st->input->radio, (float) bit_errors_p1_fm(st->viterbi_p1, st->scrambler_p1) / P1_FRAME_LEN_ENCODED_FM);
    descramble(st->scrambler_p1, P1_FRAME_LEN_FM);
    frame_push(&st->input->frame, st->scrambler_p1, P1_FRAME_LEN_FM, P1_LOGICAL_CHANNEL);
}

void decode_process_pids(decode_t *st)
{
    const int J = 20, B = 16, C = 36;
    const int8_t v[] = {
        10, 2, 18, 6, 14, 8, 16, 0, 12, 4,
        11, 3, 19, 7, 15, 9, 17, 1, 13, 5
    };
    unsigned int i, out = 0;
    for (i = 0; i < PIDS_FRAME_LEN_ENCODED_FM; i++)
    {
        int partition = v[i % J];
        int block = decode_get_block(st) - 1;
        int k = ((i / J) % (PIDS_FRAME_LEN_ENCODED_FM / J)) + (P1_FRAME_LEN_ENCODED_FM / (J * B));
        int row = (k * 11) % 32;
        int column = (k * 11 + k / (32*9)) % C;
        st->viterbi_pids[out++] = st->buffer_pm[(block * 32 + row) * 720 + partition * C + column];
        if ((out % 6) == 5) // depuncture, [1, 1, 1, 1, 1, 0]
            st->viterbi_pids[out++] = 0;
    }

    nrsc5_conv_decode_pids(st->viterbi_pids, st->scrambler_pids);
    descramble(st->scrambler_pids, PIDS_FRAME_LEN);
    pids_frame_push(&st->pids, st->scrambler_pids);
}

void decode_process_p3_p4(decode_t *st, interleaver_iv_t *interleaver, int8_t *viterbi, uint8_t *scrambler, unsigned int frame_len, logical_channel_t lc)
{
    const unsigned int J = (frame_len == P3_FRAME_LEN_FM) ? 4 : 2;
    const unsigned int B = 32;
    const unsigned int C = 36;
    const unsigned int M = (frame_len == P3_FRAME_LEN_FM) ? 2 : 4;
    const unsigned int N = (frame_len == P3_FRAME_LEN_FM) ? 147456 : 73728;
    const unsigned int bk_bits = 32 * C;
    const unsigned int bk_adj = 32 * C - 1;
    unsigned int i, out = 0;
    for (i = 0; i < frame_len * 2; i++)
    {
        int partition = ((interleaver->i + 2 * (M / 4)) / M) % J;
        unsigned int pti = interleaver->pt[partition]++;
        int block = (pti + (partition * 7) - (bk_adj * (pti / bk_bits))) % B;
        int row = ((11 * pti) % bk_bits) / C;
        int column = (pti * 11) % C;
        viterbi[out++] = interleaver->internal[(block * 32 + row) * (J * C) + partition * C + column];
        if ((out % 6) == 1 || (out % 6) == 4) // depuncture, [1, 0, 1, 1, 0, 1]
            viterbi[out++] = 0;

        interleaver->internal[interleaver->i] = interleaver->buffer[i];
        interleaver->i++;
    }
    if (interleaver->ready)
    {
        nrsc5_conv_decode_p3_p4(viterbi, scrambler, frame_len);
        descramble(scrambler, frame_len);
        frame_push(&st->input->frame, scrambler, frame_len, lc);
    }
    if (interleaver->i == N)
    {
        interleaver->i = 0;
        memset(interleaver->pt, 0, sizeof(unsigned int) * 4);
        interleaver->ready = 1;
    }
}

void decode_process_pids_am(decode_t *st)
{
    uint8_t il[120], iu[120];

    /* 1012s.pdf section 10.4 */
    for (int n = 0; n < 120; n++) {
        int k, p, row;

        p = n % 4;

        k = (n + (n/60) + 11) % 30;
        row = (11 * (k + (k/15)) + 3) % 32;
        il[n] = (st->buffer_pids_am[row*2] >> p) & 1;

        k = (n + (n/60)) % 30;
        row = (11 * (k + (k/15)) + 3) % 32;
        iu[n] = (st->buffer_pids_am[row*2 + 1] >> p) & 1;
    }

    /* 1012s.pdf figure 10-5 */
    for (int i = 0; i < 10; i++) {
      for (int j = 0; j < 12; j++) {
        st->viterbi_pids[i*24 + pids_il_delay[j]] = il[i*12 + j] ? 1 : -1;
        st->viterbi_pids[i*24 + pids_iu_delay[j]] = iu[i*12 + j] ? 1 : -1;
      }
    }

    nrsc5_conv_decode_e3(st->viterbi_pids, st->scrambler_pids, PIDS_FRAME_LEN);
    descramble(st->scrambler_pids, PIDS_FRAME_LEN);
    pids_frame_push(&st->pids, st->scrambler_pids);
}

void decode_process_p1_p3_am(decode_t *st)
{
    unsigned int block = st->idx_pu_pl_s_t / (PARTITION_WIDTH_AM * BLKSZ) - 1;

    if (block == 0)
        st->am_errors = 0;

    if (st->am_diversity_wait == 0)
    {
        nrsc5_conv_decode_e1(st->viterbi_p1_am + (block * P1_FRAME_LEN_AM * 3), st->scrambler_p1_am, P1_FRAME_LEN_AM);
        st->am_errors += bit_errors_p1_am(st->viterbi_p1_am + (block * P1_FRAME_LEN_AM * 3), st->scrambler_p1_am);
        descramble(st->scrambler_p1_am, P1_FRAME_LEN_AM);
        frame_push(&st->input->frame, st->scrambler_p1_am, P1_FRAME_LEN_AM, P1_LOGICAL_CHANNEL);

        if (block == 7)
        {
            if (st->input->sync.psmi != SERVICE_MODE_MA3)
            {
                nrsc5_conv_decode_e2(st->viterbi_p3_am, st->scrambler_p3_am, P3_FRAME_LEN_MA1);
                st->am_errors += bit_errors_p3_ma1(st->viterbi_p3_am, st->scrambler_p3_am);
                descramble(st->scrambler_p3_am, P3_FRAME_LEN_MA1);
                frame_push(&st->input->frame, st->scrambler_p3_am, P3_FRAME_LEN_MA1, P3_LOGICAL_CHANNEL);
        
                nrsc5_report_ber(st->input->radio, (float) st->am_errors / (8 * P1_FRAME_LEN_ENCODED_AM + P3_FRAME_LEN_ENCODED_MA1));
            }
            else
            {
                nrsc5_conv_decode_e1(st->viterbi_p3_am, st->scrambler_p3_am, P3_FRAME_LEN_MA3);
                st->am_errors += bit_errors_p3_ma3(st->viterbi_p3_am, st->scrambler_p3_am);
                descramble(st->scrambler_p3_am, P3_FRAME_LEN_MA3);
                frame_push(&st->input->frame, st->scrambler_p3_am, P3_FRAME_LEN_MA3, P3_LOGICAL_CHANNEL);
        
                nrsc5_report_ber(st->input->radio, (float) st->am_errors / (8 * P1_FRAME_LEN_ENCODED_AM + P3_FRAME_LEN_ENCODED_MA3));
            }        
        }
    }

    if (block == 7)
    {
        interleaver_ma1(st);

        if (st->am_diversity_wait > 0)
            st->am_diversity_wait--;
    }
}

void decode_set_block(decode_t *st, unsigned int bc)
{
    st->idx_pm = 720 * BLKSZ * bc;
    if (bc == 0)
        st->started_pm = 1;

    st->interleaver_px1.idx = (st->interleaver_px1.length / 2) * (bc % 2);
    st->interleaver_px2.idx = (st->interleaver_px2.length / 2) * (bc % 2);
    if ((bc % 2) == 0)
    {
        st->interleaver_px1.started = 1;
        st->interleaver_px2.started = 1;
    }
}

void decode_set_px1_length(decode_t *st, unsigned int frame_len)
{
    st->interleaver_px1.length = frame_len;
}

static void interleaver_iv_reset(interleaver_iv_t *interleaver)
{
    interleaver->idx = 0;
    interleaver->i = 0;
    memset(interleaver->pt, 0, sizeof(unsigned int) * 4);
    interleaver->length = P3_FRAME_LEN_FM * 2;
    interleaver->started = 0;
    interleaver->ready = 0;
}

void decode_reset(decode_t *st)
{
    st->idx_pm = 0;
    st->started_pm = 0;
    st->idx_pu_pl_s_t = 0;
    st->am_errors = 0;
    st->am_diversity_wait = 4;
    interleaver_iv_reset(&st->interleaver_px1);
    interleaver_iv_reset(&st->interleaver_px2);
    pids_init(&st->pids, st->input);
}

void decode_init(decode_t *st, struct input_t *input)
{
    st->input = input;
    decode_reset(st);
}
