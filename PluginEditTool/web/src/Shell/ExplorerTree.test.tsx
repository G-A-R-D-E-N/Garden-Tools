import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import type { AppInterop } from '../backend';
import ExplorerTree, { type RecordNode } from './ExplorerTree';

const group = (Key: string): RecordNode => ({
  Key, ConflictStatus: 0, IsRecordNode: false, HasChildren: true, FilePath: '',
});

afterEach(cleanup);

it('refetches expanded children when the node object is replaced and keeps the node expanded', async () => {
  const GetChildren = vi.fn()
    .mockResolvedValueOnce(JSON.stringify([group('CLMT (1)')]))
    .mockResolvedValueOnce(JSON.stringify([group('CLMT (2)')]));
  const backend = { GetChildren } as unknown as AppInterop;
  const root = group('W0.esp');
  const tree = (node: RecordNode) => (
    <ExplorerTree path="W0.esp" node={node} backend={backend} onOpenRecord={vi.fn()} />
  );
  const { container, rerender } = render(tree(root));

  fireEvent.click(screen.getByText('W0.esp'));
  expect(await screen.findByText('CLMT (1)')).toBeTruthy();

  rerender(tree({ ...root }));
  expect(await screen.findByText('CLMT (2)')).toBeTruthy();
  expect(screen.queryByText('CLMT (1)')).toBeNull();
  expect(container.querySelector('.tree-children')).not.toBeNull();
  expect(GetChildren).toHaveBeenCalledTimes(2);
  expect(GetChildren).toHaveBeenLastCalledWith('W0.esp');
});

it('does not refetch when rerendered with the same node object', async () => {
  const GetChildren = vi.fn().mockResolvedValueOnce(JSON.stringify([group('CLMT (1)')]));
  const backend = { GetChildren } as unknown as AppInterop;
  const root = group('W0.esp');
  const tree = (node: RecordNode) => (
    <ExplorerTree path="W0.esp" node={node} backend={backend} onOpenRecord={vi.fn()} />
  );
  const { rerender } = render(tree(root));

  fireEvent.click(screen.getByText('W0.esp'));
  expect(await screen.findByText('CLMT (1)')).toBeTruthy();

  rerender(tree(root));
  expect(screen.getByText('CLMT (1)')).toBeTruthy();
  expect(GetChildren).toHaveBeenCalledTimes(1);
});

it('ignores a refresh response that arrives after a newer refresh', async () => {
  const pending: Array<(json: string) => void> = [];
  const later = () => new Promise<string>(resolve => { pending.push(resolve); });
  const GetChildren = vi.fn()
    .mockResolvedValueOnce(JSON.stringify([group('CLMT (1)')]))
    .mockImplementationOnce(later)
    .mockImplementationOnce(later);
  const backend = { GetChildren } as unknown as AppInterop;
  const root = group('W0.esp');
  const tree = (node: RecordNode) => (
    <ExplorerTree path="W0.esp" node={node} backend={backend} onOpenRecord={vi.fn()} />
  );
  const { rerender } = render(tree(root));

  fireEvent.click(screen.getByText('W0.esp'));
  expect(await screen.findByText('CLMT (1)')).toBeTruthy();

  rerender(tree({ ...root }));
  rerender(tree({ ...root }));
  expect(GetChildren).toHaveBeenCalledTimes(3);

  await act(async () => { pending[1](JSON.stringify([group('CLMT (3)')])); });
  await act(async () => { pending[0](JSON.stringify([group('CLMT (2)')])); });
  expect(screen.getByText('CLMT (3)')).toBeTruthy();
  expect(screen.queryByText('CLMT (2)')).toBeNull();
});

it('clears expanded children when the refreshed node reports no children', async () => {
  const GetChildren = vi.fn().mockResolvedValueOnce(JSON.stringify([group('CLMT (1)')]));
  const backend = { GetChildren } as unknown as AppInterop;
  const root = group('W0.esp');
  const tree = (node: RecordNode) => (
    <ExplorerTree path="W0.esp" node={node} backend={backend} onOpenRecord={vi.fn()} />
  );
  const { rerender } = render(tree(root));

  fireEvent.click(screen.getByText('W0.esp'));
  expect(await screen.findByText('CLMT (1)')).toBeTruthy();

  rerender(tree({ ...root, HasChildren: false }));
  expect(screen.queryByText('CLMT (1)')).toBeNull();
  expect(GetChildren).toHaveBeenCalledTimes(1);
});

it('drops an in-flight fetch when the node is collapsed and refetches on re-expand', async () => {
  const pending: Array<(json: string) => void> = [];
  const later = () => new Promise<string>(resolve => { pending.push(resolve); });
  const GetChildren = vi.fn()
    .mockImplementationOnce(later)
    .mockResolvedValueOnce(JSON.stringify([group('CLMT (2)')]));
  const backend = { GetChildren } as unknown as AppInterop;
  const root = group('W0.esp');
  const tree = (node: RecordNode) => (
    <ExplorerTree path="W0.esp" node={node} backend={backend} onOpenRecord={vi.fn()} />
  );
  const { container } = render(tree(root));

  fireEvent.click(screen.getByText('W0.esp'));
  fireEvent.click(screen.getByText('W0.esp'));
  await act(async () => { pending[0](JSON.stringify([group('CLMT (1)')])); });
  expect(screen.queryByText('CLMT (1)')).toBeNull();
  expect(container.querySelector('.tree-loading')).toBeNull();

  fireEvent.click(screen.getByText('W0.esp'));
  expect(screen.queryByText('CLMT (1)')).toBeNull();
  expect(await screen.findByText('CLMT (2)')).toBeTruthy();
  expect(GetChildren).toHaveBeenCalledTimes(2);
  expect(GetChildren).toHaveBeenLastCalledWith('W0.esp');
});
