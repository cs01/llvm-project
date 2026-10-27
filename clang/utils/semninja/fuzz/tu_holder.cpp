int useHolder(const lib::Holder<int> &h) {
  return h.get() + (int)lib::identity<long>(2);
}
