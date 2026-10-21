int main(void) {
game_replay_Handle handle = {.fd = 5};
game_replay_Replay replay = game_replay_open(&handle, "last.replay");
float root = sqrtf(16.0f);
printf("frames=%llu name=%s length=%zu version=%u mode=%d root=%.1f eof=%d\n",
(unsigned long long)replay.frames, replay.name.data, lib_text_length(replay.name), (unsigned)game_replay_version,
game_replay_Slow, (double)root, EOF);
printf("counter=%u\n", game_replay_counter);
__UINT8_TYPE__ bytes[3] = {1, 2, 3};
printf("checksum=%llu\n", (unsigned long long)game_replay_checksum(bytes, 3));
return 0;
}
