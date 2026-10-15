#!/usr/bin/env ruby
# Origin: EmberBSD causal CIX SPCR production checks, 2026-10-15.
# SPDX-License-Identifier: BSD-2-Clause
require 'fileutils'
root = File.expand_path('../..', __dir__)
out = File.expand_path(ARGV.fetch(0))
FileUtils.mkdir_p(out)
original = File.read(File.join(root, 'sys/stand/efiboot/cix_spcr.c'))
mutations = {
  'map-rp' => ['(md.Attribute & EFI_MEMORY_RP) != 0', '0'],
  'map-type' => ['(md.Type != EfiACPIReclaimMemory &&', '(0 &&'],
  'map-overlap' => ['b.PhysicalStart < end)', 'b.PhysicalStart < end && 0)'],
  'read-range' => ['address + length > end', 'address > end'],
  'table-checksum' => ['sum(p, *length) != 0', '0'],
  'dbg2-profile' => ['memcmp(dbg2, cix_dbg2, sizeof(cix_dbg2)) != 0', 'memcmp(dbg2, cix_dbg2, sizeof(cix_dbg2)) == 12345'],
  'spcr-profile' => ['memcmp(spcr, cix_before, sizeof(cix_before)) != 0', '0'],
  'spcr-checksum' => ['checksum(spcr, sizeof(cix_before), 9);', 'spcr[9] = 0;'],
  'xsdt-reference' => ['putle(xsdt + i, dest + soff, 8);', 'putle(xsdt + i, (uintptr_t)plan.spcr, 8);'],
  'rsdt-reference' => ['putle(rsdt + i, dest + soff, 4);', 'putle(rsdt + i, (uintptr_t)plan.spcr, 4);'],
  'legacy-bound-exclusive' => ['(uint64_t)UINT32_MAX + 1 - EFI_PAGE_SIZE', 'UINT32_MAX - EFI_PAGE_SIZE'],
  'legacy-bound-removed' => ['(uint64_t)UINT32_MAX + 1 - EFI_PAGE_SIZE', 'UINT64_MAX'],
  'shadow-root-alias' => ['(root < dest + EFI_PAGE_SIZE && dest < root + 36)', '0'],
  'shadow-table-alias' => ['dest < address + length)', 'dest < address + length && 0)']
}
mutations.each do |name, (from, to)|
  raise "missing mutation #{name}" unless original.scan(from).length == 1
  dir = File.join(out, name)
  FileUtils.mkdir_p(dir)
  source = File.join(dir, 'cix_spcr.c')
  File.write(source, original.sub(from, to))
  log = File.join(dir, 'result.log')
  ok = system({'CIX_SOURCE' => source}, 'sh', File.join(root, 'ember/tests/efi-cix-spcr.sh'), dir, out: log, err: [:child, :out])
  text = File.read(log)
  raise "mutant survived #{name}" if ok
  raise "mutation did not execute #{name}: #{text}" unless text.include?('Assertion failed:') || text.include?('FAIL: expected unchanged')
  puts "PASS: killed #{name} by behavioral assertion"
end
